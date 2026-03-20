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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "core/power_controller.h"
#include "core/sol.h"
#include "hal/hal.h"

#define SOCKET_PATH     "/var/run/minibmc.sock"
#define SOL_SOCKET_PATH "/var/run/minibmc-sol.sock"

#define LOOP_INTERVAL_MS    10      /* 100 Hz */
#define HEARTBEAT_PERIOD_MS 500     /* status LED toggle rate */
#define POWERON_TIMEOUT_MS  5000    /* power-good wait limit */

#define STATE_FILE          "/var/lib/minibmc/state"

static void state_save(PowerState s) {
    mkdir("/var/lib/minibmc", 0755);
    FILE *f = fopen(STATE_FILE, "w");
    if (f) { fprintf(f, "%d\n", (int)s); fclose(f); }
}

static PowerState state_load(void) {
    FILE *f = fopen(STATE_FILE, "r");
    if (!f) return STATE_OFF;
    int v = STATE_OFF;
    fscanf(f, "%d", &v);
    fclose(f);
    if (v < 0 || v >= STATE_ERROR) return STATE_OFF;
    return (PowerState)v;
}

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
            hal_gpio_write(HAL_PIN_POWER_BUTTON, HAL_GPIO_LOW);
            hal_gpio_write(HAL_PIN_POWER_LED, HAL_GPIO_HIGH);
            hal_log(HAL_LOG_INFO, "Action: ASSERT power button");
            break;
        case ACTION_DEASSERT_POWER_BUTTON:
            hal_gpio_write(HAL_PIN_POWER_BUTTON, HAL_GPIO_HIGH);
            hal_gpio_write(HAL_PIN_POWER_LED, HAL_GPIO_LOW);
            hal_log(HAL_LOG_INFO, "Action: DEASSERT power button");
            break;
        case ACTION_NONE:
            break;
    }
}

/* Shared pending event state — used by both stdin and IPC socket handlers */
static int      pending_event = -1;
static uint32_t pending_time  = 0;

/* Read a command from stdin (non-blocking) and return a PowerEvent (or -1) */
static int bmc_poll_stdin(PowerController *ctrl) {
    static char   cmd_buf[64];
    static size_t cmd_pos = 0;

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
                    pending_event = EVENT_POWER_GOOD_RECEIVED;
                    pending_time  = hal_get_tick_ms();
                    return EVENT_POWER_BUTTON_PRESSED;
                } else {
                    hal_log(HAL_LOG_WARN, "Cannot power on — current state: %s",
                            state_names[state]);
                }
            } else if (strcmp(cmd_buf, "power off") == 0) {
                if (state == STATE_ON) {
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

/* ---- Unix socket IPC ---- */

static int ipc_server_fd = -1;
static int ipc_client_fd = -1;

static int ipc_init(void) {
    ipc_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_server_fd < 0) {
        hal_log(HAL_LOG_ERROR, "ipc socket: %s", strerror(errno));
        return -1;
    }
    fcntl(ipc_server_fd, F_SETFL, O_NONBLOCK);

    unlink(SOCKET_PATH);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(ipc_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(ipc_server_fd, 4) < 0) {
        hal_log(HAL_LOG_ERROR, "ipc bind/listen: %s", strerror(errno));
        close(ipc_server_fd);
        ipc_server_fd = -1;
        return -1;
    }
    chmod(SOCKET_PATH, 0660);
    hal_log(HAL_LOG_INFO, "IPC socket: %s", SOCKET_PATH);
    return 0;
}

static void ipc_shutdown(void) {
    if (ipc_client_fd >= 0) { close(ipc_client_fd); ipc_client_fd = -1; }
    if (ipc_server_fd >= 0) { close(ipc_server_fd); ipc_server_fd = -1; }
    unlink(SOCKET_PATH);
}

/* Process a command string, write response to fd, return PowerEvent or -1 */
static int ipc_handle_command(const char *cmd, int fd,
                              PowerController *ctrl,
                              int *pending_event, uint32_t *pending_time) {
    PowerState state = power_controller_get_state(ctrl);

    if (strcmp(cmd, "power on") == 0) {
        if (state == STATE_OFF || state == STATE_ERROR) {
            *pending_event = EVENT_POWER_GOOD_RECEIVED;
            *pending_time  = hal_get_tick_ms();
            dprintf(fd, "OK\n");
            return EVENT_POWER_BUTTON_PRESSED;
        }
        dprintf(fd, "ERROR:Cannot power on — state: %s\n", state_names[state]);
    } else if (strcmp(cmd, "power off") == 0) {
        if (state == STATE_ON) {
            *pending_event = EVENT_POWER_GOOD_RECEIVED;
            *pending_time  = hal_get_tick_ms();
            dprintf(fd, "OK\n");
            return EVENT_SHUTDOWN_REQUESTED;
        }
        dprintf(fd, "ERROR:Cannot power off — state: %s\n", state_names[state]);
    } else if (strcmp(cmd, "status") == 0) {
        dprintf(fd, "STATE:%s\n", state_names[state]);
    } else {
        dprintf(fd, "ERROR:Unknown command\n");
    }
    return -1;
}

static int ipc_poll(PowerController *ctrl,
                    int *pending_event, uint32_t *pending_time) {
    /* Accept new client if none connected */
    if (ipc_client_fd < 0 && ipc_server_fd >= 0) {
        ipc_client_fd = accept(ipc_server_fd, NULL, NULL);
        if (ipc_client_fd >= 0)
            fcntl(ipc_client_fd, F_SETFL, O_NONBLOCK);
    }
    if (ipc_client_fd < 0) return -1;

    /* Read a command line from the client */
    static char   buf[64];
    static size_t pos = 0;
    char c;
    ssize_t n;

    while ((n = read(ipc_client_fd, &c, 1)) == 1) {
        if (c == '\n') {
            buf[pos] = '\0';
            pos = 0;
            int ev = ipc_handle_command(buf, ipc_client_fd, ctrl,
                                        pending_event, pending_time);
            return ev;
        } else if (pos < sizeof(buf) - 1) {
            buf[pos++] = c;
        }
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        /* Client disconnected */
        close(ipc_client_fd);
        ipc_client_fd = -1;
        pos = 0;
    }
    return -1;
}

/* ---- SOL Unix socket (bidirectional console forwarding) ---- */

static int sol_server_fd = -1;
static int sol_sock_client_fd = -1;

static int sol_socket_init(void) {
    sol_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sol_server_fd < 0) {
        hal_log(HAL_LOG_ERROR, "sol socket: %s", strerror(errno));
        return -1;
    }
    fcntl(sol_server_fd, F_SETFL, O_NONBLOCK);

    unlink(SOL_SOCKET_PATH);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOL_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sol_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(sol_server_fd, 4) < 0) {
        hal_log(HAL_LOG_ERROR, "sol bind/listen: %s", strerror(errno));
        close(sol_server_fd);
        sol_server_fd = -1;
        return -1;
    }
    chmod(SOL_SOCKET_PATH, 0660);
    hal_log(HAL_LOG_INFO, "SOL socket: %s", SOL_SOCKET_PATH);
    return 0;
}

static void sol_socket_shutdown(void) {
    sol_set_client(-1);
    if (sol_sock_client_fd >= 0) { close(sol_sock_client_fd); sol_sock_client_fd = -1; }
    if (sol_server_fd    >= 0) { close(sol_server_fd);    sol_server_fd    = -1; }
    unlink(SOL_SOCKET_PATH);
}

/* Accept new SOL clients; forward keystrokes from client → UART */
static void sol_socket_poll(void) {
    if (sol_sock_client_fd < 0 && sol_server_fd >= 0) {
        sol_sock_client_fd = accept(sol_server_fd, NULL, NULL);
        if (sol_sock_client_fd >= 0) {
            fcntl(sol_sock_client_fd, F_SETFL, O_NONBLOCK);
            sol_set_client(sol_sock_client_fd);
            hal_log(HAL_LOG_INFO, "SOL client connected");
        }
    }
    if (sol_sock_client_fd < 0) return;

    /* Read keystrokes from the client and write them to the host UART */
    uint8_t buf[64];
    ssize_t n = read(sol_sock_client_fd, buf, sizeof(buf));
    if (n > 0) {
        hal_log(HAL_LOG_INFO, "SOL TX %zd bytes to UART: %02X %02X %02X %02X",
                n,
                n > 0 ? buf[0] : 0,
                n > 1 ? buf[1] : 0,
                n > 2 ? buf[2] : 0,
                n > 3 ? buf[3] : 0);
        for (ssize_t i = 0; i < n; i++)
            hal_uart_write_byte(buf[i]);
    } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(sol_sock_client_fd);
        sol_sock_client_fd = -1;
        sol_set_client(-1);
        hal_log(HAL_LOG_INFO, "SOL client disconnected");
    }
}

/* Poll hardware signals and return a PowerEvent (or -1 for none) */
static int bmc_poll_events(PowerController *ctrl) {
    (void)ctrl;
    /* GPIO 17 is the relay output — not a readable input.
     * GPIO 18 (POWER_GOOD) is not yet wired.
     * Events are driven entirely by bmc_poll_stdin until both are wired. */
    return -1;
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

    /* Initialize power controller and restore last known state */
    PowerController ctrl;
    power_controller_init(&ctrl);
    ctrl.current_state = state_load();

    /* Initialize Serial-Over-LAN */
    if (sol_init(115200) != 0) {
        hal_log(HAL_LOG_WARN, "SOL init failed — continuing without serial capture");
    }

    /* Initialize IPC socket for Redfish API */
    if (ipc_init() != 0) {
        hal_log(HAL_LOG_WARN, "IPC socket init failed — continuing without API socket");
    }

    /* Initialize SOL socket for WebSocket console access */
    if (sol_socket_init() != 0) {
        hal_log(HAL_LOG_WARN, "SOL socket init failed — continuing without SOL socket");
    }

    /* Set stdin non-blocking so command reads don't stall the loop */
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

    hal_log(HAL_LOG_INFO, "MiniBMC started — state: %s%s",
            state_names[power_controller_get_state(&ctrl)],
            power_controller_get_state(&ctrl) != STATE_OFF ? " (restored)" : "");
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

        /* Poll for events: stdin, IPC socket, hardware */
        int event = bmc_poll_stdin(&ctrl);
        if (event < 0)
            event = ipc_poll(&ctrl, &pending_event, &pending_time);
        if (event < 0)
            event = bmc_poll_events(&ctrl);
        if (event >= 0) {
            PowerState old_state = power_controller_get_state(&ctrl);
            PowerAction act = power_controller_handle_event(&ctrl, (PowerEvent)event);
            PowerState new_state = power_controller_get_state(&ctrl);

            if (old_state != new_state) {
                hal_log(HAL_LOG_INFO, "State: %s -> %s",
                        state_names[old_state], state_names[new_state]);
                state_save(new_state);
            }
            bmc_execute_action(act);
        }

        /* Poll SOL socket — accept clients, forward keystrokes to UART */
        sol_socket_poll();

        /* Poll serial console — always capture regardless of power state */
        sol_poll(true);

        hal_delay_ms(LOOP_INTERVAL_MS);
    }

    hal_log(HAL_LOG_INFO, "MiniBMC shutting down");
    ipc_shutdown();
    sol_socket_shutdown();
    sol_shutdown();
    hal_gpio_write(HAL_PIN_POWER_LED,  HAL_GPIO_LOW);
    hal_gpio_write(HAL_PIN_STATUS_LED, HAL_GPIO_LOW);
    hal_shutdown();

    return 0;
}
