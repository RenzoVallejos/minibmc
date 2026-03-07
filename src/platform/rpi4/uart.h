/*
 * uart.h — UART device and pin definitions for Raspberry Pi 4.
 * Defines the default serial device path and the GPIO pins used for
 * TX/RX when connecting to a host's serial console.
 */
#ifndef RPI4_UART_H
#define RPI4_UART_H

/* UART device for host serial console (PL011 on BCM2711) */
#define RPI4_UART_DEVICE    "/dev/ttyAMA0"

/* GPIO pin assignments for UART */
#define RPI4_UART_TX_PIN    14   /* GPIO14 — TXD0 (ALT0) */
#define RPI4_UART_RX_PIN    15   /* GPIO15 — RXD0 (ALT0) */

/* Default baud rate for host serial console */
#define RPI4_UART_DEFAULT_BAUD  115200

#endif
