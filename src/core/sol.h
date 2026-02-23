#ifndef SOL_H
#define SOL_H

#include <stddef.h>
#include <stdint.h>

int    sol_init(uint32_t baud_rate);
void   sol_poll(void);
size_t sol_read(uint8_t *dst, size_t max_len);
size_t sol_available(void);
void   sol_dump(void);
void   sol_shutdown(void);

#endif
