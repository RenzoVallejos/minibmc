/*
 * test_power_controller.c — Unit tests for the power state machine.
 * Covers all state transitions, edge cases, and a full power cycle.
 * 8 tests total. No external dependencies.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/power_controller.h"

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

TEST(test_initial_state_is_off) {
    PowerController ctrl;
    power_controller_init(&ctrl);
    ASSERT_EQ(STATE_OFF, power_controller_get_state(&ctrl));
}

TEST(test_power_button_starts_powering_on) {
    PowerController ctrl;
    power_controller_init(&ctrl);
    PowerAction act = power_controller_handle_event(&ctrl, EVENT_POWER_BUTTON_PRESSED);
    ASSERT_EQ(STATE_POWERING_ON, power_controller_get_state(&ctrl));
    ASSERT_EQ(ACTION_ASSERT_POWER_BUTTON, act);
}

TEST(test_power_good_completes_power_on) {
    PowerController ctrl;
    power_controller_init(&ctrl);
    power_controller_handle_event(&ctrl, EVENT_POWER_BUTTON_PRESSED);
    PowerAction act = power_controller_handle_event(&ctrl, EVENT_POWER_GOOD_RECEIVED);
    ASSERT_EQ(STATE_ON, power_controller_get_state(&ctrl));
    ASSERT_EQ(ACTION_DEASSERT_POWER_BUTTON, act);
}

TEST(test_shutdown_request_starts_shutting_down) {
    PowerController ctrl;
    power_controller_init(&ctrl);
    power_controller_handle_event(&ctrl, EVENT_POWER_BUTTON_PRESSED);
    power_controller_handle_event(&ctrl, EVENT_POWER_GOOD_RECEIVED);
    PowerAction act = power_controller_handle_event(&ctrl, EVENT_SHUTDOWN_REQUESTED);
    ASSERT_EQ(STATE_SHUTTING_DOWN, power_controller_get_state(&ctrl));
    ASSERT_EQ(ACTION_ASSERT_POWER_BUTTON, act);
}

TEST(test_power_lost_completes_shutdown) {
    PowerController ctrl;
    power_controller_init(&ctrl);
    power_controller_handle_event(&ctrl, EVENT_POWER_BUTTON_PRESSED);
    power_controller_handle_event(&ctrl, EVENT_POWER_GOOD_RECEIVED);
    power_controller_handle_event(&ctrl, EVENT_SHUTDOWN_REQUESTED);
    PowerAction act = power_controller_handle_event(&ctrl, EVENT_POWER_GOOD_RECEIVED);
    ASSERT_EQ(STATE_OFF, power_controller_get_state(&ctrl));
    ASSERT_EQ(ACTION_DEASSERT_POWER_BUTTON, act);
}

TEST(test_timeout_in_powering_on_causes_error) {
    PowerController ctrl;
    power_controller_init(&ctrl);
    power_controller_handle_event(&ctrl, EVENT_POWER_BUTTON_PRESSED);
    PowerAction act = power_controller_handle_event(&ctrl, EVENT_TIMEOUT);
    ASSERT_EQ(STATE_ERROR, power_controller_get_state(&ctrl));
    ASSERT_EQ(ACTION_NONE, act);
}

TEST(test_invalid_events_ignored) {
    PowerController ctrl;
    power_controller_init(&ctrl);
    /* Sending POWER_GOOD while OFF should do nothing */
    PowerAction act = power_controller_handle_event(&ctrl, EVENT_POWER_GOOD_RECEIVED);
    ASSERT_EQ(STATE_OFF, power_controller_get_state(&ctrl));
    ASSERT_EQ(ACTION_NONE, act);
    /* Sending TIMEOUT while OFF should do nothing */
    act = power_controller_handle_event(&ctrl, EVENT_TIMEOUT);
    ASSERT_EQ(STATE_OFF, power_controller_get_state(&ctrl));
    ASSERT_EQ(ACTION_NONE, act);
}

TEST(test_full_power_cycle) {
    PowerController ctrl;
    power_controller_init(&ctrl);

    /* OFF -> POWERING_ON */
    ASSERT_EQ(STATE_OFF, power_controller_get_state(&ctrl));
    power_controller_handle_event(&ctrl, EVENT_POWER_BUTTON_PRESSED);
    ASSERT_EQ(STATE_POWERING_ON, power_controller_get_state(&ctrl));

    /* POWERING_ON -> ON */
    power_controller_handle_event(&ctrl, EVENT_POWER_GOOD_RECEIVED);
    ASSERT_EQ(STATE_ON, power_controller_get_state(&ctrl));

    /* ON -> SHUTTING_DOWN */
    power_controller_handle_event(&ctrl, EVENT_SHUTDOWN_REQUESTED);
    ASSERT_EQ(STATE_SHUTTING_DOWN, power_controller_get_state(&ctrl));

    /* SHUTTING_DOWN -> OFF */
    power_controller_handle_event(&ctrl, EVENT_POWER_GOOD_RECEIVED);
    ASSERT_EQ(STATE_OFF, power_controller_get_state(&ctrl));
}

/* ---- Runner ---- */

int main(void) {
    printf("Running power controller tests...\n");

    RUN_TEST(test_initial_state_is_off);
    RUN_TEST(test_power_button_starts_powering_on);
    RUN_TEST(test_power_good_completes_power_on);
    RUN_TEST(test_shutdown_request_starts_shutting_down);
    RUN_TEST(test_power_lost_completes_shutdown);
    RUN_TEST(test_timeout_in_powering_on_causes_error);
    RUN_TEST(test_invalid_events_ignored);
    RUN_TEST(test_full_power_cycle);

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
