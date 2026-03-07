/*
 * sol.c — Serial-Over-LAN console capture implementation.
 * Reads bytes from the UART (via HAL) into a 4096-byte ring buffer.
 * Complete lines are logged in real time so the BMC can display host
 * boot messages (POST output, kernel log, etc.) as they arrive.
 */
#include "sol.h"
#include "ring_buffer.h"
#include "../hal/hal.h"

#include <string.h>

#define SOL_BUFFER_SIZE  4096
#define SOL_LINE_MAX     256

static uint8_t    sol_backing[SOL_BUFFER_SIZE];
static RingBuffer sol_ring;

static char    line_buf[SOL_LINE_MAX];
static size_t  line_pos;

int sol_init(uint32_t baud_rate) {
    ring_buffer_init(&sol_ring, sol_backing, SOL_BUFFER_SIZE);
    line_pos = 0;
    return hal_uart_init(baud_rate);
}

void sol_poll(bool host_is_on) {
    uint8_t byte;
    while (hal_uart_read_byte(&byte)) {
        if (!host_is_on) continue;  /* drain UART but discard when host is off */

        ring_buffer_put(&sol_ring, byte);

        if (byte == '\n' || byte == '\r') {
            if (line_pos > 0) {
                line_buf[line_pos] = '\0';
                hal_log(HAL_LOG_INFO, "[SOL] %s", line_buf);
                line_pos = 0;
            }
        } else {
            if (line_pos < SOL_LINE_MAX - 1) {
                line_buf[line_pos++] = (char)byte;
            }
        }
    }
}

size_t sol_read(uint8_t *dst, size_t max_len) {
    return ring_buffer_read(&sol_ring, dst, max_len);
}

size_t sol_available(void) {
    return ring_buffer_count(&sol_ring);
}

void sol_dump(void) {
    if (ring_buffer_is_empty(&sol_ring)) {
        hal_log(HAL_LOG_INFO, "[SOL] Buffer empty");
        return;
    }

    hal_log(HAL_LOG_INFO, "[SOL] Buffer dump (%zu bytes):", ring_buffer_count(&sol_ring));

    /* Non-destructive peek: walk from tail without modifying the buffer */
    size_t count = sol_ring.count;
    size_t pos   = sol_ring.tail;
    char   dump_line[SOL_LINE_MAX];
    size_t dl = 0;

    for (size_t i = 0; i < count; i++) {
        uint8_t b = sol_ring.buf[pos];
        pos = (pos + 1) % sol_ring.capacity;

        if (b == '\n' || b == '\r') {
            if (dl > 0) {
                dump_line[dl] = '\0';
                hal_log(HAL_LOG_INFO, "[SOL]   %s", dump_line);
                dl = 0;
            }
        } else {
            if (dl < SOL_LINE_MAX - 1)
                dump_line[dl++] = (char)b;
        }
    }
    if (dl > 0) {
        dump_line[dl] = '\0';
        hal_log(HAL_LOG_INFO, "[SOL]   %s", dump_line);
    }
}

void sol_shutdown(void) {
    hal_uart_shutdown();
    ring_buffer_reset(&sol_ring);
    line_pos = 0;
}
