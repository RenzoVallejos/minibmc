/*
 * hal_uart_stub.c — Stub HAL for SOL unit tests.
 * Replaces the real UART and logging HAL functions with minimal fakes.
 * Call hal_uart_stub_load() to pre-load bytes the stub will return
 * one at a time when hal_uart_read_byte() is called.
 */
#include "../src/hal/hal.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ---- Stub UART ---- */

static const uint8_t *stub_data;
static size_t         stub_len;
static size_t         stub_pos;

void hal_uart_stub_load(const uint8_t *data, size_t len) {
    stub_data = data;
    stub_len  = len;
    stub_pos  = 0;
}

int hal_uart_init(uint32_t baud_rate) {
    (void)baud_rate;
    return 0;
}

void hal_uart_shutdown(void) {
    stub_data = NULL;
    stub_len  = 0;
    stub_pos  = 0;
}

int hal_uart_read_byte(uint8_t *byte) {
    if (stub_data == NULL || stub_pos >= stub_len)
        return 0;
    *byte = stub_data[stub_pos++];
    return 1;
}

void hal_uart_write_byte(uint8_t byte) {
    (void)byte;
}

/* ---- Stub logging ---- */

void hal_log(HalLogLevel level, const char *fmt, ...) {
    (void)level;
    (void)fmt;
    /* silent in tests */
}
