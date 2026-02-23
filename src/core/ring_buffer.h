#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t   capacity;
    size_t   head;      /* next write position */
    size_t   tail;      /* next read position  */
    size_t   count;     /* current byte count  */
} RingBuffer;

void   ring_buffer_init(RingBuffer *rb, uint8_t *backing, size_t capacity);
void   ring_buffer_put(RingBuffer *rb, uint8_t byte);
bool   ring_buffer_get(RingBuffer *rb, uint8_t *byte);
size_t ring_buffer_read(RingBuffer *rb, uint8_t *dst, size_t max_len);
bool   ring_buffer_peek(RingBuffer *rb, uint8_t *byte);
void   ring_buffer_reset(RingBuffer *rb);
bool   ring_buffer_is_empty(const RingBuffer *rb);
bool   ring_buffer_is_full(const RingBuffer *rb);
size_t ring_buffer_count(const RingBuffer *rb);
size_t ring_buffer_free(const RingBuffer *rb);

#endif
