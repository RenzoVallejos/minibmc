#ifndef RPI4_GPIO_H
#define RPI4_GPIO_H

#include <stdint.h>

/*
 * BCM2711 GPIO register definitions for Raspberry Pi 4.
 * Base address when accessed via /dev/gpiomem (offset 0).
 * Physical base: 0xFE200000.
 */

#define RPI4_GPIO_BASE      0xFE200000
#define RPI4_GPIO_LEN       0xF4

/* Register offsets (bytes) */
#define GPFSEL0             0x00
#define GPFSEL1             0x04
#define GPFSEL2             0x08
#define GPSET0              0x1C
#define GPCLR0              0x28
#define GPLEV0              0x34

/* GPIO function select values */
#define GPIO_FUNC_INPUT     0x0
#define GPIO_FUNC_OUTPUT    0x1

/* Pin assignments */
#define RPI4_PIN_POWER_BUTTON   17   /* Input:  momentary push button */
#define RPI4_PIN_POWER_GOOD     18   /* Input:  ATX power good signal */
#define RPI4_PIN_POWER_LED      22   /* Output: power indicator LED   */
#define RPI4_PIN_STATUS_LED     23   /* Output: BMC heartbeat LED     */

/* Inline helpers operating on a volatile base pointer */

static inline void rpi4_gpio_set_function(volatile uint32_t *base,
                                          unsigned pin, unsigned func) {
    unsigned reg   = pin / 10;
    unsigned shift = (pin % 10) * 3;
    volatile uint32_t *gpfsel = base + reg;          /* GPFSEL0..GPFSEL5 */
    uint32_t val = *gpfsel;
    val &= ~(0x7u << shift);
    val |= (func & 0x7u) << shift;
    *gpfsel = val;
}

static inline void rpi4_gpio_set(volatile uint32_t *base, unsigned pin) {
    *(base + GPSET0 / 4) = 1u << pin;
}

static inline void rpi4_gpio_clear(volatile uint32_t *base, unsigned pin) {
    *(base + GPCLR0 / 4) = 1u << pin;
}

static inline uint32_t rpi4_gpio_read(volatile uint32_t *base, unsigned pin) {
    return (*(base + GPLEV0 / 4) >> pin) & 1u;
}

#endif
