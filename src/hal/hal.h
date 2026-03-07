/*
 * hal.h — Hardware Abstraction Layer interface.
 * Defines the platform-independent API for GPIO, UART, timing, and logging.
 * Implemented by hal_sim.c (simulation) and hal_rpi4.c (Raspberry Pi 4).
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stdint.h>

/* GPIO Pins (logical names — mapped to physical pins per platform) */
typedef enum {
    HAL_PIN_POWER_BUTTON,
    HAL_PIN_POWER_GOOD,
    HAL_PIN_POWER_LED,
    HAL_PIN_STATUS_LED,
    HAL_PIN_COUNT
} HalGpioPin;

/* GPIO States */
typedef enum {
    HAL_GPIO_LOW  = 0,
    HAL_GPIO_HIGH = 1
} HalGpioState;

/* Log Levels */
typedef enum {
    HAL_LOG_DEBUG,
    HAL_LOG_INFO,
    HAL_LOG_WARN,
    HAL_LOG_ERROR
} HalLogLevel;

/* Lifecycle */
int  hal_init(void);
void hal_shutdown(void);

/* GPIO */
void         hal_gpio_write(HalGpioPin pin, HalGpioState state);
HalGpioState hal_gpio_read(HalGpioPin pin);

/* Timing */
void     hal_delay_ms(uint32_t ms);
uint32_t hal_get_tick_ms(void);

/* UART */
int  hal_uart_init(uint32_t baud_rate);
void hal_uart_shutdown(void);
int  hal_uart_read_byte(uint8_t *byte);   /* non-blocking: returns 1 if read, 0 if empty */
void hal_uart_write_byte(uint8_t byte);

/* Logging */
void hal_log(HalLogLevel level, const char *fmt, ...);

#endif
