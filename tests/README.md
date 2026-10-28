# Mimic tests

Sources and checks that run cc09 output on mimic09.

- `asm/` : 6809 assembly for `a9`. `acia-hello.asm` is the ACIA banner. `make acia-test` assembles it, links it, and runs it.
- `c/` : C sources. `hello.c` prints one line through `printf`. `make hello` compiles it with `xc9`, links `cc09/lib/rnlib9.lib`, and runs it on the ACIA at `0xF940`.
- `instr/` : instruction checks, including immediate `LDQ` writing `A`, `B`, `E`, and `F`. `./tests/run-instr-suite.sh` runs them.
- `linker/` : linker directive and map checks.
- `gen_acia_bin.py` : older helper that emits ACIA machine-code bytes. `make acia-test` does not use it.

Run from the Mimic root:

```
make
make hello
make acia-test
make test
```

`make acia-test` uses `cc09/bin/a9`, `cc09/bin/xld`, and `mimic09` with `--acia 0xF940`. `make hello` uses `xc9` and the same ACIA. `cstart` restarts `main`, so the hello run stops after the banner appears.

## Instruction Set Validation (new)

`tests/instr/validate.py` (with `tests/run-instr-suite.sh` wrapper) contains automated tests that:

- For each instruction + addressing mode:
  - "Enter the opcodes and operands by hand" into memory using the monitor's `:` command (exactly as a human would in the debugger).
  - Set up initial register state (via `;` edit command).
  - Single-step the instruction (`S`).
  - Capture final register state (`R`).
  - Compare against expected values derived from the Motorola 6809 / Hitachi 6309 programming reference manuals (flag update rules, register effects, etc.).
- Before/after register states (A,B,D,X,Y,S,U,PC,CC,E,F,Q,MD,...) are printed for every test.
- Each test can be run individually or the whole suite.
- The suite prints Pass/Fail for **each test as it runs** + a final summary.

Run from project root:

    ./tests/run-instr-suite.sh                 # full suite (6809 + 6309)
    ./tests/run-instr-suite.sh --list
    ./tests/run-instr-suite.sh --test 6809_ABX_inh

The script uses the built `mimic09/bin/mimic09`. Rebuild with `make -C mimic09` if needed.

Add more tests by extending the TESTS list in `tests/instr/validate.py`. All tests are self-contained and documented with the source reference for the expected behavior.
