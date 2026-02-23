# MiniBMC

A minimal Baseboard Management Controller (BMC) implementation featuring a power state machine, Serial-Over-LAN console capture, hardware abstraction layer, and Raspberry Pi 4 GPIO/UART support.

## Architecture

```
┌──────────────────────────────────────────────┐
│                  main.c                      │
│           (event loop, signals)              │
├──────────────┬───────────────┬───────────────┤
│ power_       │  sol          │     HAL       │
│ controller   │  (serial      │  (hal.h       │
│ (state       │   console     │   interface)  │
│  machine)    │   capture)    │               │
│              ├───────────────┤               │
│              │ ring_buffer   ├───────┬───────┤
│              │ (byte FIFO)   │hal_sim│hal_rpi│
└──────────────┴───────────────┴───────┴───────┘
```

**Power State Machine:**
```
OFF ──[button]──> POWERING_ON ──[power_good]──> ON
 ^                    │                         │
 │                [timeout]                 [shutdown]
 │                    v                         v
 │                  ERROR              SHUTTING_DOWN
 └────────────────[power_lost]──────────────────┘
```

**SOL Data Flow:**
```
Host UART ──> hal_uart_read_byte() ──> ring_buffer ──> sol_read() / log
       (or PTY in sim mode)
```

## Project Structure

```
minibmc/
├── Makefile
├── README.md
├── src/
│   ├── main.c                  ← event loop + HAL + SOL integration
│   ├── core/
│   │   ├── power_controller.c  ← power state machine
│   │   ├── power_controller.h
│   │   ├── ring_buffer.c       ← circular byte buffer
│   │   ├── ring_buffer.h
│   │   ├── sol.c               ← Serial-Over-LAN console capture
│   │   └── sol.h
│   ├── hal/
│   │   ├── hal.h               ← platform-independent HAL interface
│   │   ├── hal_sim.c           ← simulation backend (PTY + fake POST)
│   │   └── hal_rpi4.c          ← Raspberry Pi 4 backend (GPIO + UART)
│   └── platform/
│       └── rpi4/
│           ├── gpio.h          ← BCM2711 GPIO register definitions
│           └── uart.h          ← UART device/pin definitions
└── tests/
    ├── test_power_controller.c ← 8 power state machine tests
    ├── test_ring_buffer.c      ← 8 ring buffer tests
    ├── test_sol.c              ← 4 SOL tests
    └── hal_uart_stub.c         ← stub HAL for SOL tests
```

## Build

```bash
# Simulation (default — works on any machine)
make

# Raspberry Pi 4 (native or cross-compile)
make PLATFORM=rpi4
make PLATFORM=rpi4 CC=aarch64-linux-gnu-gcc

# Run unit tests (20 total)
make test

# Clean
make clean
```

## Run

```bash
# Simulation mode — runs event loop with simulated GPIO and serial console
./minibmc
# After simulated power-on, fake POST messages scroll automatically
# Ctrl+C to shut down cleanly
```

## Serial-Over-LAN (SOL)

The SOL module captures serial console output from the host system (or simulated POST messages in sim mode) into a 4096-byte ring buffer. Complete lines are logged in real time.

**Simulation mode** creates a PTY (pseudo-terminal) on startup. You can interact with it:

```bash
# Start MiniBMC — note the PTY path in the startup log
./minibmc
# Output: UART PTY opened: /dev/ttys004

# In another terminal, send data to the PTY
echo "Hello from host" > /dev/ttys004
# MiniBMC will display: [SOL] Hello from host
```

Fake POST messages are automatically generated after the simulated power-on, paced at ~1 message per 500ms.

**RPi4 mode** opens `/dev/ttyAMA0` at the configured baud rate (default 115200) to capture real serial output from an attached host (e.g., Arduino or another SBC).

## Pin Assignments (Raspberry Pi 4)

| Signal       | GPIO | BCM Pin | Direction | Description              |
|-------------|------|---------|-----------|--------------------------|
| POWER_BUTTON | 17   | GPIO17  | Input     | Momentary push button    |
| POWER_GOOD   | 18   | GPIO18  | Input     | ATX power good signal    |
| POWER_LED    | 22   | GPIO22  | Output    | Power indicator LED      |
| STATUS_LED   | 23   | GPIO23  | Output    | BMC heartbeat LED        |
| UART_TX      | 14   | GPIO14  | Output    | Serial console TX        |
| UART_RX      | 15   | GPIO15  | Input     | Serial console RX        |

## Wiring (RPi4)

```
RPi4 GPIO Header
─────────────────
GPIO17 (pin 11) ← Push button → GND (pin 9)
GPIO18 (pin 12) ← ATX PG signal (active high)
GPIO22 (pin 15) → 330Ω → LED → GND (power)
GPIO23 (pin 16) → 330Ω → LED → GND (heartbeat)
GPIO14 (pin 8)  → Host RX (serial console)
GPIO15 (pin 10) ← Host TX (serial console)
```

## Skills Demonstrated

- **Embedded C**: Bare-metal GPIO/UART register manipulation via mmap
- **Hardware Abstraction**: Platform-independent HAL enabling simulation and real hardware
- **State Machine Design**: Event-driven power controller with timeout handling
- **Ring Buffers**: Lock-free circular buffer for streaming console data
- **UART/Serial**: Non-blocking serial I/O with PTY simulation
- **Systems Programming**: Signal handling, memory-mapped I/O, pseudo-terminals
- **Build Systems**: Makefile with cross-compilation and platform selection
- **Testing**: 20 unit tests with custom assert framework, no external dependencies
