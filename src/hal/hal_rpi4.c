/*
 * hal_rpi4.c — Raspberry Pi 4 HAL backend.
 * GPIO is controlled via memory-mapped BCM2711 registers (/dev/gpiomem).
 * UART reads from a serial device (default /dev/ttyAMA0, overridden by
 * the MINIBMC_UART environment variable, e.g. /dev/ttyACM0 for USB serial).
 */
#include "hal.h"
#include "../platform/rpi4/gpio.h"
#include "../platform/rpi4/uart.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <errno.h>

static volatile uint32_t *gpio_base;
static int mem_fd = -1;
static int uart_fd = -1;
static struct timespec start_time;

/* Map logical HAL pins to physical BCM2711 GPIO numbers */
static const unsigned pin_map[HAL_PIN_COUNT] = {
    [HAL_PIN_POWER_BUTTON] = RPI4_PIN_POWER_BUTTON,
    [HAL_PIN_POWER_GOOD]   = RPI4_PIN_POWER_GOOD,
    [HAL_PIN_POWER_LED]    = RPI4_PIN_POWER_LED,
    [HAL_PIN_STATUS_LED]   = RPI4_PIN_STATUS_LED
};

static const char *level_prefix[] = {
    [HAL_LOG_DEBUG] = "DEBUG",
    [HAL_LOG_INFO]  = "INFO",
    [HAL_LOG_WARN]  = "WARN",
    [HAL_LOG_ERROR] = "ERROR"
};

int hal_init(void) {
    mem_fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/gpiomem");
        return -1;
    }

    gpio_base = (volatile uint32_t *)mmap(NULL, RPI4_GPIO_LEN,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED, mem_fd, 0);
    if (gpio_base == MAP_FAILED) {
        perror("mmap gpio");
        close(mem_fd);
        mem_fd = -1;
        return -1;
    }

    /* Configure pin directions */
    rpi4_gpio_set_function(gpio_base, RPI4_PIN_POWER_BUTTON, GPIO_FUNC_INPUT);
    rpi4_gpio_set_function(gpio_base, RPI4_PIN_POWER_GOOD,   GPIO_FUNC_INPUT);
    rpi4_gpio_set_function(gpio_base, RPI4_PIN_POWER_LED,    GPIO_FUNC_OUTPUT);
    rpi4_gpio_set_function(gpio_base, RPI4_PIN_STATUS_LED,   GPIO_FUNC_OUTPUT);

    /* Start outputs LOW */
    rpi4_gpio_clear(gpio_base, RPI4_PIN_POWER_LED);
    rpi4_gpio_clear(gpio_base, RPI4_PIN_STATUS_LED);

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    hal_log(HAL_LOG_INFO, "HAL RPi4 initialized (BCM2711 GPIO)");
    return 0;
}

void hal_shutdown(void) {
    if (gpio_base && gpio_base != MAP_FAILED) {
        /* Turn off outputs */
        rpi4_gpio_clear(gpio_base, RPI4_PIN_POWER_LED);
        rpi4_gpio_clear(gpio_base, RPI4_PIN_STATUS_LED);

        munmap((void *)gpio_base, RPI4_GPIO_LEN);
        gpio_base = NULL;
    }
    if (mem_fd >= 0) {
        close(mem_fd);
        mem_fd = -1;
    }
    hal_log(HAL_LOG_INFO, "HAL RPi4 shutdown");
}

void hal_gpio_write(HalGpioPin pin, HalGpioState state) {
    if (pin >= HAL_PIN_COUNT || !gpio_base) return;
    unsigned hw_pin = pin_map[pin];
    if (state == HAL_GPIO_HIGH) {
        rpi4_gpio_set(gpio_base, hw_pin);
    } else {
        rpi4_gpio_clear(gpio_base, hw_pin);
    }
}

HalGpioState hal_gpio_read(HalGpioPin pin) {
    if (pin >= HAL_PIN_COUNT || !gpio_base) return HAL_GPIO_LOW;
    return rpi4_gpio_read(gpio_base, pin_map[pin]) ? HAL_GPIO_HIGH : HAL_GPIO_LOW;
}

void hal_delay_ms(uint32_t ms) {
    usleep((useconds_t)ms * 1000);
}

uint32_t hal_get_tick_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t sec  = (uint32_t)(now.tv_sec  - start_time.tv_sec);
    uint32_t msec = (uint32_t)((now.tv_nsec - start_time.tv_nsec) / 1000000);
    return sec * 1000 + msec;
}

/* ---- UART ---- */

static speed_t baud_to_speed(uint32_t baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B115200;
    }
}

int hal_uart_init(uint32_t baud_rate) {
    const char *dev = getenv("MINIBMC_UART");
    if (!dev) dev = RPI4_UART_DEVICE;

    uart_fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (uart_fd < 0) {
        hal_log(HAL_LOG_ERROR, "Failed to open %s: %s", dev, strerror(errno));
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(uart_fd, &tty) != 0) {
        hal_log(HAL_LOG_ERROR, "tcgetattr failed: %s", strerror(errno));
        close(uart_fd);
        uart_fd = -1;
        return -1;
    }

    speed_t speed = baud_to_speed(baud_rate);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    cfmakeraw(&tty);

    /* 8N1 */
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;

    /* Non-blocking reads */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(uart_fd, TCSANOW, &tty) != 0) {
        hal_log(HAL_LOG_ERROR, "tcsetattr failed: %s", strerror(errno));
        close(uart_fd);
        uart_fd = -1;
        return -1;
    }

    hal_log(HAL_LOG_INFO, "UART initialized: %s @ %u baud", dev, baud_rate);
    return 0;
}

void hal_uart_shutdown(void) {
    if (uart_fd >= 0) {
        close(uart_fd);
        hal_log(HAL_LOG_INFO, "UART closed");
        uart_fd = -1;
    }
}

int hal_uart_read_byte(uint8_t *byte) {
    if (uart_fd < 0) return 0;
    ssize_t n = read(uart_fd, byte, 1);
    return (n == 1) ? 1 : 0;
}

void hal_uart_write_byte(uint8_t byte) {
    if (uart_fd >= 0) {
        ssize_t ret = write(uart_fd, &byte, 1);
        (void)ret;
    }
}

void hal_log(HalLogLevel level, const char *fmt, ...) {
    uint32_t tick = hal_get_tick_ms();
    fprintf(stderr, "[%5u.%03u] %-5s: ", tick / 1000, tick % 1000, level_prefix[level]);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
