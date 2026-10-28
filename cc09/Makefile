# Makefile for cc09 - Wintek 6809 cross tools recreation (a9, xld, xc9 etc.)
# Builds native tools for macOS and Linux/UNIX from src/
# Does not touch anything under WINTEK/

CC      ?= cc
# _DEFAULT_SOURCE and _POSIX_C_SOURCE keep readlink, realpath, and
# strcasecmp declared on current glibc. Keep both if you override CFLAGS.
CFLAGS  ?= -O2 -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
# For debug: CFLAGS += -g

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

TOOLS   = a9 xld xc9 xc_1 xc9_3 hex   # hex is the PROM filter from HEX.EXE

.PHONY: all clean install uninstall test dirs asm-test link-test legacy-smoke

all: dirs $(addprefix bin/,$(TOOLS))

dirs:
	mkdir -p bin

COMMON_HDR = src/common/srec.h src/common/util.h src/common/opcode6809.h src/common/sections.h
COMMON_SRC = src/common/srec.c src/common/util.c src/common/opcode6809.c src/common/sections.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

bin/a9: src/a9/a9.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -Isrc/common -o $@ $^

bin/xld: src/xld/xld.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -Isrc/common -o $@ $^

bin/xc9: src/xc9/xc9.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -Isrc/common -o $@ $^

bin/xc_1: src/xc9/xc_1.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -Isrc/common -o $@ $^

bin/xc9_3: src/xc9/xc9_3.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -Isrc/common -o $@ $^

bin/hex: src/common/hex.c
	$(CC) $(CFLAGS) -o $@ $^

src/common/%.o: src/common/%.c $(COMMON_HDR)
	$(CC) $(CFLAGS) -Isrc/common -c -o $@ $<

# Ensure minimal sections implementation exists (committed small file)
src/common/sections.c: src/common/sections.h

src/common/sections.h:
	@true

clean:
	rm -f bin/* src/common/*.o
	rm -rf bin

install: all
	mkdir -p $(BINDIR)
	cp bin/a9 bin/xld bin/xc9 bin/xc_1 bin/xc9_3 bin/hex $(BINDIR)/
	@echo "Installed to $(BINDIR)"

uninstall:
	rm -f $(BINDIR)/a9 $(BINDIR)/xld $(BINDIR)/xc9 $(BINDIR)/xc_1 $(BINDIR)/xc9_3 $(BINDIR)/hex

test: all legacy-smoke
	@echo "=== Testing CLI banners (should match original style) ==="
	@bin/a9 2>&1 | cat || true
	@echo "---"
	@bin/xld 2>&1 | cat || true
	@echo "---"
	@bin/xc9 2>&1 | cat || true
	@echo "---"
	@bin/hex 2>&1 | cat || true
	@echo "=== Basic structure and build OK ==="
	@ls -l bin/
	@echo "For real tests once implemented: make asm-test ; etc."

asm-test: bin/a9
	mkdir -p /tmp/mimic-a9
	bin/a9 lib/src/cstart.asm -nh -o /tmp/mimic-a9/cstart.rel
	bin/a9 lib/src/chario.asm -nh -o /tmp/mimic-a9/chario.rel
	# Goldens carry one extra trailing CRLF. Compare the S-record lines.
	tr -d '\r' < /tmp/mimic-a9/cstart.rel | grep . > /tmp/mimic-a9/cstart.n
	tr -d '\r' < tests/golden/cstart_real.rel | grep . > /tmp/mimic-a9/cstart.g
	diff -u /tmp/mimic-a9/cstart.g /tmp/mimic-a9/cstart.n
	tr -d '\r' < /tmp/mimic-a9/chario.rel | grep . > /tmp/mimic-a9/chario.n
	tr -d '\r' < tests/golden/chario_real.rel | grep . > /tmp/mimic-a9/chario.g
	diff -u /tmp/mimic-a9/chario.g /tmp/mimic-a9/chario.n
	@echo "a9 reloc objects match cstart and chario goldens"

link-test: bin/xld
	mkdir -p /tmp/mimic-a9
	bin/xld -nh tests/golden/cstart_real.rel -o /tmp/mimic-a9/cstart.mik -map /tmp/mimic-a9/cstart.map
	tr -d '\r' < /tmp/mimic-a9/cstart.mik | grep . > /tmp/mimic-a9/cstart.mik.n
	tr -d '\r' < tests/golden/cstart_real.mik | grep . > /tmp/mimic-a9/cstart.mik.g
	diff -u /tmp/mimic-a9/cstart.mik.g /tmp/mimic-a9/cstart.mik.n
	grep -F 'ASCT = 0002' /tmp/mimic-a9/cstart.map
	grep -F 'BSCT = 0000' /tmp/mimic-a9/cstart.map
	grep -F 'DSCT = 0100' /tmp/mimic-a9/cstart.map
	grep -F 'PSCT = 000E' /tmp/mimic-a9/cstart.map
	grep -F 'Entry Point at: 0200' /tmp/mimic-a9/cstart.map
	grep -F '.clrio' /tmp/mimic-a9/cstart.map
	grep -F 'main' /tmp/mimic-a9/cstart.map
	@echo "xld cstart image and section sizes match the golden"

legacy-smoke: bin/a9 bin/xld bin/hex
	python3 tests/smoke/check_legacy.py

# To compare behavior with original, use dosbox + the untouched WINTEK/*.EXE on
# the staged lib/src/*.asm files (or crafted tests under tests/).

