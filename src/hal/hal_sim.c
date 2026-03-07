/*
 * hal_sim.c — Simulation HAL backend.
 * GPIO state is kept in memory. UART opens a PTY (pseudo-terminal) so you
 * can send data to the BMC from another terminal, and feeds fake POST boot
 * messages automatically once the simulated host is powered on.
 */
#include "hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __linux__
#include <pty.h>
#elif defined(__APPLE__)
#include <util.h>
#endif

static HalGpioState gpio_state[HAL_PIN_COUNT];

/* ---- UART / PTY simulation ---- */
static int pty_master_fd = -1;

static const char *fake_post_messages[] = {
    "POST: BIOS initializing...\r\n",
    "POST: Memory check... 2048 MB OK\r\n",
    "POST: PCI bus scan... 3 devices found\r\n",
    "POST: USB init... 2 ports detected\r\n",
    "POST: Boot device: /dev/sda1\r\n",
    "GRUB: Loading kernel...\r\n",
    "Linux: Booting kernel 6.1.0-minibmc\r\n",
    "Linux: Mounting root filesystem... OK\r\n",
    "Linux: Starting init process...\r\n",
    "systemd: Reached target Multi-User System\r\n",
    "login: \r\n",
    NULL
};

static int           post_msg_index;
static size_t        post_byte_offset;
static uint32_t      post_last_time;

static const char *pin_names[] = {
    [HAL_PIN_POWER_BUTTON] = "POWER_BUTTON",
    [HAL_PIN_POWER_GOOD]   = "POWER_GOOD",
    [HAL_PIN_POWER_LED]    = "POWER_LED",
    [HAL_PIN_STATUS_LED]   = "STATUS_LED"
};

static const char *level_prefix[] = {
    [HAL_LOG_DEBUG] = "DEBUG",
    [HAL_LOG_INFO]  = "INFO",
    [HAL_LOG_WARN]  = "WARN",
    [HAL_LOG_ERROR] = "ERROR"
};

static struct timespec start_time;

int hal_init(void) {
    for (int i = 0; i < HAL_PIN_COUNT; i++) {
        gpio_state[i] = HAL_GPIO_LOW;
    }
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    hal_log(HAL_LOG_INFO, "HAL simulation initialized");
    return 0;
}

void hal_shutdown(void) {
    hal_log(HAL_LOG_INFO, "HAL simulation shutdown");
}

void hal_gpio_write(HalGpioPin pin, HalGpioState state) {
    if (pin >= HAL_PIN_COUNT) return;
    gpio_state[pin] = state;
    hal_log(HAL_LOG_DEBUG, "GPIO %s <- %s", pin_names[pin],
            state == HAL_GPIO_HIGH ? "HIGH" : "LOW");
}

HalGpioState hal_gpio_read(HalGpioPin pin) {
    if (pin >= HAL_PIN_COUNT) return HAL_GPIO_LOW;
    return gpio_state[pin];
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

int hal_uart_init(uint32_t baud_rate) {
    (void)baud_rate;

    pty_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty_master_fd < 0) {
        hal_log(HAL_LOG_ERROR, "posix_openpt failed: %s", strerror(errno));
        return -1;
    }
    if (grantpt(pty_master_fd) != 0 || unlockpt(pty_master_fd) != 0) {
        hal_log(HAL_LOG_ERROR, "grantpt/unlockpt failed");
        close(pty_master_fd);
        pty_master_fd = -1;
        return -1;
    }

    /* Set non-blocking */
    int flags = fcntl(pty_master_fd, F_GETFL, 0);
    fcntl(pty_master_fd, F_SETFL, flags | O_NONBLOCK);

    post_msg_index  = 0;
    post_byte_offset = 0;
    post_last_time  = hal_get_tick_ms();

    hal_log(HAL_LOG_INFO, "UART PTY opened: %s", ptsname(pty_master_fd));
    return 0;
}

void hal_uart_shutdown(void) {
    if (pty_master_fd >= 0) {
        close(pty_master_fd);
        hal_log(HAL_LOG_INFO, "UART PTY closed");
        pty_master_fd = -1;
    }
}

int hal_uart_read_byte(uint8_t *byte) {
    /* Try real PTY data first */
    if (pty_master_fd >= 0) {
        ssize_t n = read(pty_master_fd, byte, 1);
        if (n == 1) return 1;
    }

    /* Feed fake POST messages when host is powered on */
    if (gpio_state[HAL_PIN_POWER_LED] != HAL_GPIO_HIGH)
        return 0;  /* host not on */

    if (fake_post_messages[post_msg_index] == NULL)
        return 0;  /* all messages sent */

    uint32_t now = hal_get_tick_ms();
    if (post_byte_offset == 0 && now - post_last_time < 500)
        return 0;  /* pace: one message per 500ms */

    *byte = (uint8_t)fake_post_messages[post_msg_index][post_byte_offset];
    post_byte_offset++;

    if (fake_post_messages[post_msg_index][post_byte_offset] == '\0') {
        post_msg_index++;
        post_byte_offset = 0;
        post_last_time = now;
    }

    return 1;
}

void hal_uart_write_byte(uint8_t byte) {
    if (pty_master_fd >= 0) {
        ssize_t ret = write(pty_master_fd, &byte, 1);
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
