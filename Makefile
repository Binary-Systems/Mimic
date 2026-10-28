# Mimic top-level Makefile
# Orchestrates the two sub-projects: cc09 (toolchain) and mimic09 (emulator)
# Primary mission: produce and validate "Hello World" (and more) that
#   compiles/assembles via cc09 tools and executes correctly under mimic09,
#   exercising the ACIA for stdio (getc/putc/printf etc).

SUBS = cc09 mimic09

.PHONY: all build clean test test-cc09 test-mimic09 integration hello acia-test help

all: build shebang

build:
	@echo "=== Building cc09 toolchain ==="
	$(MAKE) -C cc09
	@echo "=== Building mimic09 emulator ==="
	$(MAKE) -C mimic09
	@echo "=== Build complete ==="
	@ls -l cc09/bin/ mimic09/bin/

# Install / refresh the shebang driver (mimic-cc09) into root bin/ so that
#   #!/usr/bin/env mimic-cc09
# works after "make" (user may still need PATH=bin:$PATH for direct ./hello.c exec).
shebang:
	@mkdir -p bin
	@if [ -f bin/mimic-cc09 ]; then chmod +x bin/mimic-cc09; fi
	@echo "Shebang driver: bin/mimic-cc09 (use as interpreter or PATH=bin:\$$PATH ./prog.c)"

clean:
	$(MAKE) -C cc09 clean
	$(MAKE) -C mimic09 clean
	rm -f tests/asm/*.lst tests/asm/*.rel tests/asm/*.map tests/asm/*.mik tests/asm/*.s19 tests/asm/*.hex tests/asm/*.bin
	rm -f tests/c/*.lst tests/c/*.rel tests/c/*.map tests/c/*.mik tests/c/*.s19 tests/c/*.hex tests/c/*.bin
	rm -rf tmp/ *.lst *.rel *.map *.mik *.s19 *.hex *.bin

test: test-cc09 test-mimic09
	@echo "=== Subproject tests complete ==="

test-cc09:
	@echo "=== cc09 tests ==="
	$(MAKE) -C cc09 test
	$(MAKE) -C cc09 asm-test
	$(MAKE) -C cc09 link-test
	$(MAKE) -C cc09 legacy-smoke

test-mimic09:
	@echo "=== mimic09 tests ==="
	$(MAKE) -C mimic09 test
	$(MAKE) -C mimic09 run

# Integration targets (new test files live under ./tests/ here at Mimic root)
integration: build acia-test
	@echo "=== Integration tests (cc09 output x mimic09) complete ==="

# ACIA hello: a9 assembles tests/asm/acia-hello.asm, xld links it, mimic09 runs it.
# The banner must come from that image. gen_acia_bin.py stays in the tree and is not used.
acia-test: build
	@mkdir -p tmp
	@echo "=== ACIA hello (a9 + xld image, ACIA at 0xF940) ==="
	cc09/bin/a9 tests/asm/acia-hello.asm -nh -l tmp/acia-hello.lst -o tmp/acia-hello.rel
	cc09/bin/xld -nh tmp/acia-hello.rel -o tmp/acia-hello.mik -map tmp/acia-hello.map
	mimic09/bin/mimic09 -l tmp/acia-hello.mik --acia 0xF940 -g > tmp/acia-run.log
	@cat tmp/acia-run.log
	grep -q 'HI!' tmp/acia-run.log
	@echo "ACIA hello banner found"

# C Hello World: xc9 compiles tests/c/hello.c, mimic09 prints the banner.
# cstart restarts main, so the run is cut off once the text has appeared.
hello: build
	@mkdir -p tmp
	@echo "=== C hello (xc9 + mimic09, ACIA at 0xF940) ==="
	cc09/bin/xc9 -o tmp/c-hello.mik -map tmp/c-hello.map tests/c/hello.c
	python3 -c 'import subprocess,sys; exec("try:\n out=subprocess.run([\"mimic09/bin/mimic09\",\"-l\",\"tmp/c-hello.mik\",\"--acia\",\"0xF940\",\"-g\"],capture_output=True,timeout=1.0).stdout\nexcept subprocess.TimeoutExpired as e:\n out=e.stdout or b\"\"\nopen(\"tmp/c-hello.log\",\"wb\").write(out)\nsys.stdout.buffer.write(out)\nraise SystemExit(0 if b\"Hello, world from shebang C via cc09 + mimic09 ACIA!\" in out else 1)")'
	@echo "C hello banner found"

help:
	@echo "Mimic combined project"
	@echo "  make              - build both subprojects"
	@echo "  make build        - same"
	@echo "  make test         - run sub tests"
	@echo "  make integration  - cross tests (acia etc)"
	@echo "  make install      - build + install toolchain to $(PREFIX)/bin (default /usr/local)"
	@echo "  make install PREFIX=/opt/mimic"
	@echo "  sudo make install   # typical for /usr/local"
	@echo "  make clean"
	@echo "See AGENTS.md (root + per subdir) for full mission."

# Demo the shebang "compile + run in emulator" on the C hello (after make + PATH=bin:$PATH)
demo-hello:
	@echo "=== Shebang demo: ./tests/c/hello.c (requires PATH=bin:\$$PATH) ==="
	PATH=bin:$$PATH ./tests/c/hello.c || PATH=bin:$$PATH bin/mimic-cc09 tests/c/hello.c

# Richer ACIA test (echo/getc): assemble a small echo program that reads chars via
# the chario getchar/getc path and writes them back (exercises RX+TX over pipes).
acia-echo:
	@mkdir -p tests/asm tmp
	@echo "NAM aciaecho" > tmp/echo.asm
	@echo "GLOBAL main" >> tmp/echo.asm
	@echo "EXTERN getchar, putchar" >> tmp/echo.asm
	@echo "PSCT" >> tmp/echo.asm
	@echo "main  BSR getchar ; BSR putchar ; BRA main" >> tmp/echo.asm
	@echo "ASCT" >> tmp/echo.asm
	@echo "ORG \$$FFFE" >> tmp/echo.asm
	@echo "FDB main" >> tmp/echo.asm
	@echo "END main" >> tmp/echo.asm
	cc09/bin/a9 tmp/echo.asm -o tmp/echo.rel
	cc09/bin/xld tmp/echo.rel cc09/tests/golden/cstart_real.rel -o tmp/echo.mik || true
	@echo "Built tmp/echo.mik (echo via getchar/putchar). Test with pipe:"
	@echo '  printf "abc\r" | mimic09/bin/mimic09 -l tmp/echo.mik --acia 0xF940 -g'

# --------------------------------------------------------------------
# Install / Uninstall
# --------------------------------------------------------------------
# Install the full Mimic toolchain (cc09 tools + mimic09 + shebang driver)
# into $(PREFIX)/bin (default /usr/local/bin).
#
# Usage:
#   make install
#   make install PREFIX=/opt/mimic
#   sudo make install PREFIX=/usr/local
#   make install DESTDIR=/tmp/stage PREFIX=/usr/local   # for packaging
#
# The subproject Makefiles already know how to install their tools.
# We also install the mimic-cc09 driver (the shebang interpreter).

PREFIX  ?= /usr/local
DESTDIR ?=

.PHONY: install uninstall

install: build
	@echo "=== Installing Mimic toolchain to $(DESTDIR)$(PREFIX)/bin ==="
	$(MAKE) -C cc09 install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	$(MAKE) -C mimic09 install PREFIX=$(PREFIX) DESTDIR=$(DESTDIR)
	@install -d $(DESTDIR)$(PREFIX)/bin
	@install -m 755 bin/mimic-cc09 $(DESTDIR)$(PREFIX)/bin/mimic-cc09
	@echo ""
	@echo "Installed:"
	@ls -l $(DESTDIR)$(PREFIX)/bin/a9 \
	        $(DESTDIR)$(PREFIX)/bin/xld \
	        $(DESTDIR)$(PREFIX)/bin/xc9 \
	        $(DESTDIR)$(PREFIX)/bin/hex \
	        $(DESTDIR)$(PREFIX)/bin/mimic09 \
	        $(DESTDIR)$(PREFIX)/bin/mimic \
	        $(DESTDIR)$(PREFIX)/bin/mimic-cc09 2>/dev/null || true
	@echo ""
	@echo "You can now run:"
	@echo "  a9, xld, xc9, hex, mimic09 (or mimic), mimic-cc09"
	@echo "Example: echo 'hello' | mimic09 --acia 0xF940 -l yourprog.mik -g"

uninstall:
	@echo "=== Uninstalling Mimic toolchain from $(DESTDIR)$(PREFIX)/bin ==="
	$(MAKE) -C cc09 uninstall PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) || true
	$(MAKE) -C mimic09 uninstall PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) || true
	@rm -f $(DESTDIR)$(PREFIX)/bin/mimic-cc09
	@echo "Mimic tools removed (if they were installed by make install)."
