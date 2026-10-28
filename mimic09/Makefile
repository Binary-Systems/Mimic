# mimic09 — fast 6809/6309 emulator + C64-style monitor + ACIA/PIA
# Targets macOS and Linux/UNIX with clang or gcc

CC ?= cc
# _DEFAULT_SOURCE keeps cfmakeraw and strcasecmp visible under -std=c11.
# Current glibc hides them without it, and GCC 14 rejects the implicit calls.
CFLAGS_BASE := -std=c11 -Wall -Wextra -Wpedantic -D_DEFAULT_SOURCE -I. -Isrc
LDFLAGS :=

# Detect OS for optional flags
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  # Apple Silicon + Intel macOS
  CFLAGS_BASE += -mmacosx-version-min=10.15
endif

# Modes
ifeq ($(MODE),debug)
  CFLAGS := $(CFLAGS_BASE) -O0 -g -DDEBUG=1
else
  # Release: very fast
  CFLAGS := $(CFLAGS_BASE) -O3 -flto -DNDEBUG=1
  ifneq ($(UNAME),Darwin)
    # On Linux allow -march=native if available (safe for most dev machines)
    CFLAGS += -march=native
  else
    # On macOS -march=native is usually fine too with Apple clang
    CFLAGS += -march=native
  endif
endif

# Output
BIN_DIR := bin
BUILD_DIR := build
TARGET := $(BIN_DIR)/mimic09

SRCS := \
  src/main.c \
  src/cpu.c \
  src/disasm.c \
  src/asm.c \
  src/debug.c \
  src/screen.c \
  src/loader.c \
  src/acia.c \
  src/pia.c \
  src/util.c

OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all release debug clean install test monitor-test run

all: release

release:
	@$(MAKE) MODE=release $(TARGET)

debug:
	@$(MAKE) MODE=debug $(TARGET)

$(TARGET): $(BIN_DIR) $(BUILD_DIR) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Built $@"

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)/mimic09 $(BIN_DIR)/mimic

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/mimic09
	ln -sf mimic09 $(DESTDIR)$(PREFIX)/bin/mimic || true
	@echo "Installed mimic09 to $(DESTDIR)$(PREFIX)/bin/"

test: $(TARGET)
	../tests/run-instr-suite.sh

monitor-test: $(TARGET)
	python3 ../tests/monitor/supermon64.py --binary $(TARGET)
	python3 ../tests/monitor/screen_edit.py --binary $(TARGET)
	python3 ../tests/monitor/asm6309.py --binary $(TARGET)

run: $(TARGET)
	./$(TARGET) -h

# Convenience: build debug and drop into interactive
idb: debug
	./$(TARGET) -i

# Show size and basic info (useful for perf curiosity)
size: $(TARGET)
	size $(TARGET)
	file $(TARGET)
