/*
 * test_ring_buffer.c — Unit tests for the circular byte buffer.
 * Tests init, put/get, fill, overflow/overwrite, multi-read, peek,
 * wraparound, and reset. 8 tests total.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/ring_buffer.h"

/* Simple test framework */
static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do {                                      \
    tests_run++;                                                  \
    printf("  %-45s ", #name);                                    \
    name();                                                       \
    tests_passed++;                                               \
    printf("PASS\n");                                             \
} while (0)

#define ASSERT_EQ(expected, actual) do {                          \
    if ((expected) != (actual)) {                                 \
        printf("FAIL\n    %s:%d: expected %d, got %d\n",         \
               __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        exit(1);                                                  \
    }                                                             \
} while (0)

#define ASSERT_TRUE(cond) do {                                    \
    if (!(cond)) {                                                \
        printf("FAIL\n    %s:%d: expected true\n",               \
               __FILE__, __LINE__);                               \
        exit(1);                                                  \
    }                                                             \
} while (0)

#define ASSERT_FALSE(cond) do {                                   \
    if ((cond)) {                                                 \
        printf("FAIL\n    %s:%d: expected false\n",              \
               __FILE__, __LINE__);                               \
        exit(1);                                                  \
    }                                                             \
} while (0)

/* ---- Tests ---- */

TEST(test_init_empty) {
    uint8_t buf[8];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));
    ASSERT_TRUE(ring_buffer_is_empty(&rb));
    ASSERT_FALSE(ring_buffer_is_full(&rb));
    ASSERT_EQ(0, (int)ring_buffer_count(&rb));
    ASSERT_EQ(8, (int)ring_buffer_free(&rb));
}

TEST(test_put_get_single) {
    uint8_t buf[8];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));

    ring_buffer_put(&rb, 0xAB);
    ASSERT_EQ(1, (int)ring_buffer_count(&rb));

    uint8_t byte;
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(0xAB, byte);
    ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

TEST(test_fill_to_capacity) {
    uint8_t buf[4];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));

    for (uint8_t i = 0; i < 4; i++)
        ring_buffer_put(&rb, i + 1);

    ASSERT_TRUE(ring_buffer_is_full(&rb));
    ASSERT_EQ(4, (int)ring_buffer_count(&rb));
    ASSERT_EQ(0, (int)ring_buffer_free(&rb));

    uint8_t byte;
    for (uint8_t i = 0; i < 4; i++) {
        ASSERT_TRUE(ring_buffer_get(&rb, &byte));
        ASSERT_EQ(i + 1, byte);
    }
}

TEST(test_overwrite_when_full) {
    uint8_t buf[4];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));

    /* Fill with 1,2,3,4 */
    for (uint8_t i = 1; i <= 4; i++)
        ring_buffer_put(&rb, i);

    /* Overwrite: put 5 — should drop 1 */
    ring_buffer_put(&rb, 5);
    ASSERT_TRUE(ring_buffer_is_full(&rb));
    ASSERT_EQ(4, (int)ring_buffer_count(&rb));

    uint8_t byte;
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(2, byte);  /* oldest surviving */
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(3, byte);
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(4, byte);
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(5, byte);  /* newest */
}

TEST(test_read_multiple) {
    uint8_t buf[8];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));

    for (uint8_t i = 0; i < 5; i++)
        ring_buffer_put(&rb, 'A' + i);

    uint8_t dst[8];
    size_t n = ring_buffer_read(&rb, dst, sizeof(dst));
    ASSERT_EQ(5, (int)n);
    ASSERT_EQ('A', dst[0]);
    ASSERT_EQ('E', dst[4]);
    ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

TEST(test_peek_no_consume) {
    uint8_t buf[8];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));

    ring_buffer_put(&rb, 0x42);

    uint8_t byte;
    ASSERT_TRUE(ring_buffer_peek(&rb, &byte));
    ASSERT_EQ(0x42, byte);
    ASSERT_EQ(1, (int)ring_buffer_count(&rb));  /* still there */

    ASSERT_TRUE(ring_buffer_peek(&rb, &byte));
    ASSERT_EQ(0x42, byte);
}

TEST(test_wraparound) {
    uint8_t buf[4];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));

    /* Fill and drain to move head/tail forward */
    for (uint8_t i = 0; i < 3; i++)
        ring_buffer_put(&rb, i);
    uint8_t byte;
    for (int i = 0; i < 3; i++)
        ring_buffer_get(&rb, &byte);

    /* Now head=3, tail=3. Fill again — wraps around */
    ring_buffer_put(&rb, 0xAA);
    ring_buffer_put(&rb, 0xBB);
    ring_buffer_put(&rb, 0xCC);
    ring_buffer_put(&rb, 0xDD);

    ASSERT_TRUE(ring_buffer_is_full(&rb));
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(0xAA, byte);
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(0xBB, byte);
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(0xCC, byte);
    ASSERT_TRUE(ring_buffer_get(&rb, &byte));
    ASSERT_EQ(0xDD, byte);
    ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

TEST(test_reset) {
    uint8_t buf[8];
    RingBuffer rb;
    ring_buffer_init(&rb, buf, sizeof(buf));

    for (uint8_t i = 0; i < 5; i++)
        ring_buffer_put(&rb, i);

    ring_buffer_reset(&rb);
    ASSERT_TRUE(ring_buffer_is_empty(&rb));
    ASSERT_EQ(0, (int)ring_buffer_count(&rb));
    ASSERT_EQ(8, (int)ring_buffer_free(&rb));

    uint8_t byte;
    ASSERT_FALSE(ring_buffer_get(&rb, &byte));
}

/* ---- Runner ---- */

int main(void) {
    printf("Running ring buffer tests...\n");

    RUN_TEST(test_init_empty);
    RUN_TEST(test_put_get_single);
    RUN_TEST(test_fill_to_capacity);
    RUN_TEST(test_overwrite_when_full);
    RUN_TEST(test_read_multiple);
    RUN_TEST(test_peek_no_consume);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_reset);

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
