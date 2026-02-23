#include "ring_buffer.h"

void ring_buffer_init(RingBuffer *rb, uint8_t *backing, size_t capacity) {
    rb->buf      = backing;
    rb->capacity = capacity;
    rb->head     = 0;
    rb->tail     = 0;
    rb->count    = 0;
}

void ring_buffer_put(RingBuffer *rb, uint8_t byte) {
    rb->buf[rb->head] = byte;
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->count == rb->capacity) {
        rb->tail = (rb->tail + 1) % rb->capacity;  /* overwrite oldest */
    } else {
        rb->count++;
    }
}

bool ring_buffer_get(RingBuffer *rb, uint8_t *byte) {
    if (rb->count == 0) return false;
    *byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;
    return true;
}

size_t ring_buffer_read(RingBuffer *rb, uint8_t *dst, size_t max_len) {
    size_t n = 0;
    while (n < max_len && rb->count > 0) {
        dst[n++] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
    }
    return n;
}

bool ring_buffer_peek(RingBuffer *rb, uint8_t *byte) {
    if (rb->count == 0) return false;
    *byte = rb->buf[rb->tail];
    return true;
}

void ring_buffer_reset(RingBuffer *rb) {
    rb->head  = 0;
    rb->tail  = 0;
    rb->count = 0;
}

bool ring_buffer_is_empty(const RingBuffer *rb) {
    return rb->count == 0;
}

bool ring_buffer_is_full(const RingBuffer *rb) {
    return rb->count == rb->capacity;
}

size_t ring_buffer_count(const RingBuffer *rb) {
    return rb->count;
}

size_t ring_buffer_free(const RingBuffer *rb) {
    return rb->capacity - rb->count;
}
