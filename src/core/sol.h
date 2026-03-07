/*
 * sol.h — Public API for Serial-Over-LAN console capture.
 * Provides functions to initialize the UART, poll for incoming bytes,
 * read buffered data, and query how much data is available.
 */
#ifndef SOL_H
#define SOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int    sol_init(uint32_t baud_rate);
void   sol_poll(bool host_is_on);
size_t sol_read(uint8_t *dst, size_t max_len);
size_t sol_available(void);
void   sol_dump(void);
void   sol_shutdown(void);

#endif
