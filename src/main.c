/*
 * main.c — Entry point for MiniBMC.
 * Sets up signal handling, initializes the HAL, power controller, and SOL,
 * then runs the main 100 Hz event loop that drives the power state machine
 * and polls the serial console.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "core/power_controller.h"
#include "core/sol.h"
#include "hal/hal.h"

#define LOOP_INTERVAL_MS    10      /* 100 Hz */
#define HEARTBEAT_PERIOD_MS 500     /* status LED toggle rate */
#define POWERON_TIMEOUT_MS  5000    /* power-good wait limit */

static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static const char *state_names[] = {
    [STATE_OFF]           = "OFF",
    [STATE_POWERING_ON]   = "POWERING_ON",
    [STATE_ON]            = "ON",
    [STATE_SHUTTING_DOWN]  = "SHUTTING_DOWN",
    [STATE_ERROR]         = "ERROR"
};

/* Translate a PowerAction into HAL GPIO calls */
static void bmc_execute_action(PowerAction action) {
    switch (action) {
        case ACTION_ASSERT_POWER_BUTTON:
            hal_gpio_write(HAL_PIN_POWER_LED, HAL_GPIO_HIGH);
            hal_log(HAL_LOG_INFO, "Action: ASSERT power button");
            break;
        case ACTION_DEASSERT_POWER_BUTTON:
            hal_gpio_write(HAL_PIN_POWER_LED, HAL_GPIO_LOW);
            hal_log(HAL_LOG_INFO, "Action: DEASSERT power button");
            break;
        case ACTION_NONE:
            break;
    }
}

/* Read a command from stdin (non-blocking) and return a PowerEvent (or -1) */
static int bmc_poll_stdin(PowerController *ctrl) {
    static char     cmd_buf[64];
    static size_t   cmd_pos = 0;
    static int      pending_event = -1;
    static uint32_t pending_time  = 0;

    /* Fire a pending simulated event after 500ms (e.g. POWER_GOOD after power on) */
    if (pending_event >= 0 && hal_get_tick_ms() - pending_time >= 500) {
        int ev = pending_event;
        pending_event = -1;
        return ev;
    }

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n') {
            cmd_buf[cmd_pos] = '\0';
            cmd_pos = 0;

            PowerState state = power_controller_get_state(ctrl);

            if (strcmp(cmd_buf, "power on") == 0) {
                if (state == STATE_OFF || state == STATE_ERROR) {
                    /* Schedule POWER_GOOD in 500ms to complete the ON transition */
                    pending_event = EVENT_POWER_GOOD_RECEIVED;
                    pending_time  = hal_get_tick_ms();
                    return EVENT_POWER_BUTTON_PRESSED;
                } else {
                    hal_log(HAL_LOG_WARN, "Cannot power on — current state: %s",
                            state_names[state]);
                }
            } else if (strcmp(cmd_buf, "power off") == 0) {
                if (state == STATE_ON) {
                    /* Schedule POWER_GOOD (power lost) in 500ms to complete shutdown */
                    pending_event = EVENT_POWER_GOOD_RECEIVED;
                    pending_time  = hal_get_tick_ms();
                    return EVENT_SHUTDOWN_REQUESTED;
                } else {
                    hal_log(HAL_LOG_WARN, "Cannot power off — current state: %s",
                            state_names[state]);
                }
            } else if (strcmp(cmd_buf, "status") == 0) {
                hal_log(HAL_LOG_INFO, "State: %s", state_names[state]);
            } else if (strlen(cmd_buf) > 0) {
                hal_log(HAL_LOG_WARN, "Unknown command: '%s' (try: power on, power off, status)",
                        cmd_buf);
            }
        } else {
            if (cmd_pos < sizeof(cmd_buf) - 1)
                cmd_buf[cmd_pos++] = c;
        }
    }
    return -1;
}

/* Poll hardware signals and return a PowerEvent (or -1 for none) */
static int bmc_poll_events(PowerController *ctrl) {
    PowerState state = power_controller_get_state(ctrl);

    /* Check power button (active high, edge detect via HAL) */
    if (hal_gpio_read(HAL_PIN_POWER_BUTTON) == HAL_GPIO_HIGH) {
        if (state == STATE_OFF) {
            return EVENT_POWER_BUTTON_PRESSED;
        } else if (state == STATE_ON) {
            return EVENT_SHUTDOWN_REQUESTED;
        }
    }

    /* Check power good signal */
    HalGpioState pg = hal_gpio_read(HAL_PIN_POWER_GOOD);
    if (state == STATE_POWERING_ON && pg == HAL_GPIO_HIGH) {
        return EVENT_POWER_GOOD_RECEIVED;
    }
    if (state == STATE_SHUTTING_DOWN && pg == HAL_GPIO_LOW) {
        return EVENT_POWER_GOOD_RECEIVED;  /* power lost = shutdown complete */
    }

    return -1;  /* no event */
}

int main(void) {
    /* Install signal handlers for clean shutdown */
    struct sigaction sa = { .sa_handler = signal_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Initialize HAL */
    if (hal_init() != 0) {
        fprintf(stderr, "Failed to initialize HAL\n");
        return 1;
    }

    /* Initialize power controller */
    PowerController ctrl;
    power_controller_init(&ctrl);

    /* Initialize Serial-Over-LAN */
    if (sol_init(115200) != 0) {
        hal_log(HAL_LOG_WARN, "SOL init failed — continuing without serial capture");
    }

    /* Set stdin non-blocking so command reads don't stall the loop */
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

    hal_log(HAL_LOG_INFO, "MiniBMC started — state: %s",
            state_names[power_controller_get_state(&ctrl)]);
    hal_log(HAL_LOG_INFO, "Commands: 'power on', 'power off', 'status'");

    uint32_t last_heartbeat = hal_get_tick_ms();
    uint32_t poweron_start  = 0;
    bool     heartbeat_on   = false;

    /* Main event loop — 100 Hz */
    while (running) {
        uint32_t now = hal_get_tick_ms();

        /* Heartbeat: toggle status LED */
        if (now - last_heartbeat >= HEARTBEAT_PERIOD_MS) {
            heartbeat_on = !heartbeat_on;
            hal_gpio_write(HAL_PIN_STATUS_LED,
                           heartbeat_on ? HAL_GPIO_HIGH : HAL_GPIO_LOW);
            last_heartbeat = now;
        }

        /* Power-on timeout check */
        if (power_controller_get_state(&ctrl) == STATE_POWERING_ON) {
            if (poweron_start == 0) {
                poweron_start = now;
            } else if (now - poweron_start >= POWERON_TIMEOUT_MS) {
                hal_log(HAL_LOG_WARN, "Power-on timeout!");
                PowerAction act = power_controller_handle_event(&ctrl, EVENT_TIMEOUT);
                bmc_execute_action(act);
                poweron_start = 0;
            }
        } else {
            poweron_start = 0;
        }

        /* Poll for hardware events or stdin commands */
        int event = bmc_poll_stdin(&ctrl);
        if (event < 0)
            event = bmc_poll_events(&ctrl);
        if (event >= 0) {
            PowerState old_state = power_controller_get_state(&ctrl);
            PowerAction act = power_controller_handle_event(&ctrl, (PowerEvent)event);
            PowerState new_state = power_controller_get_state(&ctrl);

            if (old_state != new_state) {
                hal_log(HAL_LOG_INFO, "State: %s -> %s",
                        state_names[old_state], state_names[new_state]);
            }
            bmc_execute_action(act);
        }

        /* Poll serial console — always capture regardless of power state */
        sol_poll(true);

        hal_delay_ms(LOOP_INTERVAL_MS);
    }

    hal_log(HAL_LOG_INFO, "MiniBMC shutting down");
    sol_shutdown();
    hal_gpio_write(HAL_PIN_POWER_LED,  HAL_GPIO_LOW);
    hal_gpio_write(HAL_PIN_STATUS_LED, HAL_GPIO_LOW);
    hal_shutdown();

    return 0;
}
