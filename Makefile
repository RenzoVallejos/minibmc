# MiniBMC Build System
# Usage:
#   make                          — build simulation binary (default)
#   make PLATFORM=rpi4            — build with RPi4 HAL
#   make test                     — compile and run unit tests
#   make clean                    — remove build artifacts
#   make PLATFORM=rpi4 CC=aarch64-linux-gnu-gcc  — cross-compile for RPi4

CC       ?= gcc
CFLAGS   := -Wall -Wextra -Werror -std=c11 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -O2 -g
PLATFORM ?= sim

TARGET   := minibmc
BUILDDIR := build

# Source files
CORE_SRC := src/core/power_controller.c src/core/ring_buffer.c src/core/sol.c
MAIN_SRC := src/main.c

ifeq ($(PLATFORM),rpi4)
  HAL_SRC := src/hal/hal_rpi4.c
  CFLAGS  += -DPLATFORM_RPI4
else
  HAL_SRC := src/hal/hal_sim.c
  CFLAGS  += -DPLATFORM_SIM
endif

SRCS := $(MAIN_SRC) $(CORE_SRC) $(HAL_SRC)
OBJS := $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS))

# Test binaries
TEST_POWER_BIN      := $(BUILDDIR)/test_power_controller
TEST_RING_BIN       := $(BUILDDIR)/test_ring_buffer
TEST_SOL_BIN        := $(BUILDDIR)/test_sol

TEST_POWER_OBJS     := $(BUILDDIR)/tests/test_power_controller.o $(BUILDDIR)/core/power_controller.o
TEST_RING_OBJS      := $(BUILDDIR)/tests/test_ring_buffer.o $(BUILDDIR)/core/ring_buffer.o
TEST_SOL_OBJS       := $(BUILDDIR)/tests/test_sol.o $(BUILDDIR)/core/sol.o \
                        $(BUILDDIR)/core/ring_buffer.o $(BUILDDIR)/tests/hal_uart_stub.o

# Include paths
INCLUDES := -Isrc

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Tests
test: $(TEST_POWER_BIN) $(TEST_RING_BIN) $(TEST_SOL_BIN)
	@echo "=== Power Controller Tests ==="
	@./$(TEST_POWER_BIN)
	@echo ""
	@echo "=== Ring Buffer Tests ==="
	@./$(TEST_RING_BIN)
	@echo ""
	@echo "=== SOL Tests ==="
	@./$(TEST_SOL_BIN)

$(TEST_POWER_BIN): $(TEST_POWER_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_RING_BIN): $(TEST_RING_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_SOL_BIN): $(TEST_SOL_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -rf $(BUILDDIR) $(TARGET)
