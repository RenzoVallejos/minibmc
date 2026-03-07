/*
 * test_sol.c — Unit tests for the Serial-Over-LAN module.
 * Uses a stub HAL (hal_uart_stub.c) to feed pre-loaded bytes instead of
 * a real UART. Tests capture, available count, overflow, and empty read.
 * 4 tests total.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/sol.h"

/* Declared in hal_uart_stub.c */
extern void hal_uart_stub_load(const uint8_t *data, size_t len);

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

/* ---- Tests ---- */

TEST(test_captures_bytes) {
    sol_init(115200);
    const uint8_t data[] = "Hello\n";
    hal_uart_stub_load(data, sizeof(data) - 1);
    sol_poll(true);

    uint8_t buf[32];
    size_t n = sol_read(buf, sizeof(buf));
    ASSERT_EQ(6, (int)n);
    ASSERT_EQ('H', buf[0]);
    ASSERT_EQ('\n', buf[5]);
    sol_shutdown();
}

TEST(test_available_count) {
    sol_init(115200);
    const uint8_t data[] = "ABCD";
    hal_uart_stub_load(data, 4);
    sol_poll(true);

    ASSERT_EQ(4, (int)sol_available());

    uint8_t buf[2];
    sol_read(buf, 2);
    ASSERT_EQ(2, (int)sol_available());
    sol_shutdown();
}

TEST(test_overflow_drops_oldest) {
    sol_init(115200);

    /* Feed more than 4096 bytes */
    uint8_t big[5000];
    for (int i = 0; i < 5000; i++)
        big[i] = (uint8_t)(i & 0xFF);
    hal_uart_stub_load(big, 5000);
    sol_poll(true);

    /* Buffer should be full at 4096 */
    ASSERT_EQ(4096, (int)sol_available());

    /* First byte should be from the overwritten region */
    uint8_t byte;
    size_t n = sol_read(&byte, 1);
    ASSERT_EQ(1, (int)n);
    /* Oldest surviving byte is big[5000-4096] = big[904] */
    ASSERT_EQ((uint8_t)(904 & 0xFF), byte);
    sol_shutdown();
}

TEST(test_empty_read) {
    sol_init(115200);
    hal_uart_stub_load(NULL, 0);
    sol_poll(true);

    ASSERT_EQ(0, (int)sol_available());

    uint8_t buf[8];
    size_t n = sol_read(buf, sizeof(buf));
    ASSERT_EQ(0, (int)n);
    sol_shutdown();
}

/* ---- Runner ---- */

int main(void) {
    printf("Running SOL tests...\n");

    RUN_TEST(test_captures_bytes);
    RUN_TEST(test_available_count);
    RUN_TEST(test_overflow_drops_oldest);
    RUN_TEST(test_empty_read);

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
