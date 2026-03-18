# MiniBMC

A Baseboard Management Controller (BMC) firmware implementation written in C, targeting the Raspberry Pi 4 as a management controller for an x86 server (Dell DE051). Implements remote power control, Serial-Over-LAN console capture, an event-driven power state machine, and a platform-independent hardware abstraction layer.

---

## Overview

Modern servers include a dedicated BMC — a microcontroller that manages the host independently of the main CPU. The BMC handles remote power control, hardware monitoring, and out-of-band console access even when the host is powered off or unresponsive.

MiniBMC replicates this architecture using a Raspberry Pi 4 as the management controller. It controls the host's ATX power button via a GPIO-driven relay, captures the host's serial console output (SOL), and exposes a command interface for remote power management.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                        main.c                           │
│              100 Hz event loop, signal handling         │
├───────────────────┬─────────────────┬───────────────────┤
│  power_controller │       sol       │       HAL         │
│  (state machine)  │  (SOL capture)  │   (hal.h API)     │
│                   ├─────────────────┤                   │
│                   │   ring_buffer   ├─────────┬─────────┤
│                   │   (byte FIFO)   │ hal_sim │hal_rpi4 │
└───────────────────┴─────────────────┴─────────┴─────────┘
```

The core logic (`power_controller`, `sol`, `ring_buffer`) is fully platform-independent. The HAL interface (`hal.h`) abstracts all hardware access — `hal_rpi4.c` implements it for the Pi 4, while `hal_sim.c` implements it for simulation and unit testing on any host machine.

**Power State Machine:**
```
        [POWER_BUTTON_PRESSED]
OFF ──────────────────────────> POWERING_ON ──[POWER_GOOD]──> ON
 ^                                   │                         │
 │                               [TIMEOUT]               [SHUTDOWN_REQ]
 │                                   v                         v
 │                                 ERROR               SHUTTING_DOWN
 └───────────────────[POWER_LOST]──────────────────────────────┘
```

**SOL Data Flow:**
```
Host DB9 serial port
       │
  USB-Serial cable
       │
  /dev/ttyUSB0
       │
  hal_uart_read_byte()     ← HAL reads raw bytes from UART
       │
  sol_poll()               ← SOL buffers and outputs bytes
       │
  ring_buffer              ← 4096-byte circular buffer
       │
  Terminal output          ← host console visible on BMC
```

---

## Hardware Setup

### Components
- Raspberry Pi 4 (BMC)
- Dell DE051 desktop (managed host)
- Tolako 1-channel 5V relay module (power button control)
- USB-to-DB9 serial cable (Serial-Over-LAN)

### Wiring

**Power Button Control (GPIO 17 → Relay → ATX Power Button):**
```
RPi4 GPIO17 (pin 11) ──> Relay IN
RPi4 5V     (pin 2)  ──> Relay VCC
RPi4 GND    (pin 6)  ──> Relay GND
Relay NO ──────────────> Dell ATX power button pins
```

**Serial Console (USB-Serial → Dell DB9):**
```
RPi4 USB port ──> USB-Serial adapter ──> Dell DB9 serial port
                  (/dev/ttyUSB0)
```

### GPIO Pin Assignments

| Signal        | GPIO | Direction | Description                        |
|---------------|------|-----------|------------------------------------|
| POWER_BUTTON  | 17   | Output    | Relay control — ATX power button   |
| POWER_GOOD    | 18   | Input     | ATX power good signal (not wired yet) |
| POWER_LED     | 22   | Output    | Power state indicator LED          |
| STATUS_LED    | 23   | Output    | BMC heartbeat LED                  |

---

## Project Structure

```
minibmc/
├── Makefile
├── src/
│   ├── main.c                  ← event loop, signal handling, stdin command interface
│   ├── core/
│   │   ├── power_controller.c  ← ATX power state machine
│   │   ├── power_controller.h
│   │   ├── ring_buffer.c       ← lock-free circular byte buffer
│   │   ├── ring_buffer.h
│   │   ├── sol.c               ← Serial-Over-LAN console capture
│   │   └── sol.h
│   ├── hal/
│   │   ├── hal.h               ← platform-independent HAL interface
│   │   ├── hal_rpi4.c          ← Raspberry Pi 4 backend (gpiochip v2, UART)
│   │   └── hal_sim.c           ← simulation backend (PTY + fake POST messages)
│   └── platform/
│       └── rpi4/
│           ├── gpio.h          ← BCM2711 GPIO register definitions and mmap helpers
│           └── uart.h          ← UART device path
└── tests/
    ├── test_power_controller.c ← 8 power state machine unit tests
    ├── test_ring_buffer.c      ← 8 ring buffer unit tests
    ├── test_sol.c              ← 4 SOL unit tests
    └── hal_uart_stub.c         ← UART stub for SOL tests
```

---

## Build

**Requirements:**
- Linux or macOS for simulation/tests
- `gcc` or `aarch64-linux-gnu-gcc` for Pi 4 cross-compilation

```bash
# Run unit tests (simulation, works on any machine)
make test

# Build for Raspberry Pi 4 (native on Pi)
make PLATFORM=rpi4

# Cross-compile for Pi 4 (from x86 Linux)
make PLATFORM=rpi4 CC=aarch64-linux-gnu-gcc

# Clean build artifacts
make clean
```

---

## Usage

**On the Raspberry Pi:**

```bash
# Run MiniBMC (MINIBMC_UART overrides the default /dev/ttyAMA0)
sudo MINIBMC_UART=/dev/ttyUSB0 ./minibmc
```

**Available commands (stdin):**

```
power on     — assert power button (transitions OFF → POWERING_ON → ON)
power off    — assert power button (transitions ON → SHUTTING_DOWN → OFF)
status       — print current power state
```

**Example session:**

```
[  0.000] INFO : HAL RPi4 initialized (BCM2711 GPIO)
[  0.005] INFO : UART initialized: /dev/ttyUSB0 @ 115200 baud
[  0.005] INFO : MiniBMC started — state: OFF
[  0.005] INFO : Commands: 'power on', 'power off', 'status'
power on
[  3.210] INFO : State: OFF -> POWERING_ON
[  3.210] INFO : relay pressed (GPIO 17 HIGH, fd=6)
[  3.210] INFO : Action: ASSERT power button
[  3.714] INFO : State: POWERING_ON -> ON
[  3.714] INFO : relay released (GPIO 17 LOW)
[  3.714] INFO : Action: DEASSERT power button
```

---

## Systemd Service

```bash
# Install and enable as a system service
sudo cp minibmc.service /etc/systemd/system/
sudo systemctl enable minibmc
sudo systemctl start minibmc
```

```ini
[Unit]
Description=MiniBMC — Baseboard Management Controller
After=dev-ttyUSB0.device
Wants=dev-ttyUSB0.device

[Service]
ExecStart=/home/renzoval/minibmc/minibmc
Environment=MINIBMC_UART=/dev/ttyUSB0
Restart=on-failure
User=root

[Install]
WantedBy=multi-user.target
```

---

## Roadmap

- [ ] Wire GPIO 18 to ATX POWER_GOOD signal for hardware state detection
- [ ] Unix domain socket IPC for external command interface
- [ ] Redfish-compatible REST API (Python/Flask) over the IPC layer
- [ ] Networked SOL — stream host console over TCP/WebSocket
- [ ] Hardware monitoring (temperature, fan speed via I2C/SPI)
- [ ] IPMI over LAN support

---

## Skills Demonstrated

- **Embedded C** — bare-metal GPIO control via Linux gpiochip v2 API and memory-mapped BCM2711 registers
- **Hardware Abstraction** — platform-independent HAL enabling identical logic across real hardware and simulation
- **State Machine Design** — event-driven ATX power controller with timeout and error handling
- **Systems Programming** — signal handling, mmap I/O, non-blocking UART, pseudo-terminals
- **Ring Buffer** — circular byte buffer for non-blocking serial data capture
- **Cross-platform Build** — Makefile with platform selection and cross-compilation support
- **Unit Testing** — 20 tests with a custom assert framework, zero external dependencies
