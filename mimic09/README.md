# mimic09 — 6809/6309 emulator and Mimic SuperMon

`mimic09` runs 6809 code, and the 6309 operations the interpreter implements, on macOS and UNIX. The monitor is Mimic SuperMon. There is no prompt character. Help is `?`. `H` is hunt.

The command reference, register glass, and screen editor are [`../docs/monitor-commands.md`](../docs/monitor-commands.md). This page is the program's command line and a short copy of that behavior.

## Command line

```
mimic09 [options] [file]
```

`mimic09 -h` prints this usage on stdout and exits 0. An unknown option prints it and exits 1. `mimic09 -v` prints `mimic09 0.9 (modern re-creation)` and exits 0.

| Option | Effect |
| --- | --- |
| `-i`, `--interactive` | Enter the monitor immediately. |
| `-l`, `--load <file>` | Load an S-record, Intel hex, or raw binary file. |
| `-a`, `--addr <hex>` | Force the load address. The address is scanned as hex (`%hx`), so `C000` and `0xC000` both work. Without `-a`, S-records and Intel hex use the addresses in the file. |
| `-g`, `--go [addr]` | Run from `addr`, or from the current PC. Prints `Running. Press Ctrl-C to return.` Ctrl-C returns from the run. |
| `-c`, `--cpu 6809\|6309` | CPU model. The default is `6809`. `6309` is native mode: `E`, `F`, `V`, and `MD` are printed as digits, and `MD` starts at `03`. |
| `--acia <hex>` | Map a 6850 ACIA at this base. `chario.asm` uses `0xF940`. |
| `--pia <hex>` | Map a 6821 PIA at this base. |
| `-v`, `--version` | Print the version and exit 0. |
| `-h`, `--help` | Print the usage text and exit 0. |
| `[file]` | Loaded when `-l` was not given. |

With no `-i`, no load, and no `-g`, the process enters the monitor. A load without `-i` sets PC from the reset vector at `$FFFE` when that vector is in `$0100`–`$FEFF`, and prints `PC set from reset vector to XXXX`. It does not enter the monitor unless `-i` is also set. `-g` runs and returns. The monitor starts when `-i` is set, or when there was nothing to load and no `-g`.

```
./bin/mimic09 -h
./bin/mimic09 -i
./bin/mimic09 -l prog.mik --acia 0xF940 -g
./bin/mimic09 -c 6309 -i
```

A failed load prints `Failed to load <file>` on stderr. A successful load prints `Loaded N bytes from <file>`.

## Monitor

The first letter of a command is case-insensitive. A prefix sets the radix: `$` hex, `+` decimal, `&` octal, `%` binary, `0x` hex. No prefix means hex. A line that is only a number prints that value in all four forms.

| Command | What it does |
| --- | --- |
| `r` | Print the register names, then one `;` value line. |
| `;` | Set registers. With `=`, names are `PC`, `CC`, `A`, `B`, `D`, `E`, `F`, `W`, `Q`, `DP`, `X`, `Y`, `U`, `S`, `V`, `MD`. |
| `m [start [end]]` | Eight-byte hex and ASCII lines. One address shows 12 lines. |
| `>addr bytes` | Write bytes. A `:` stops the parse so the ASCII tail is left alone. |
| `d [start [end]]` | Disassemble. Opcode bytes use five columns. |
| `a addr mnem` | Assemble one 6809 or Hitachi 6309 instruction. |
| `f start end value` | Fill the inclusive range. |
| `g [addr]` | Run. Stops on halt, a breakpoint, or Ctrl-C. |
| `j addr` | Call that address. `RTS` returns to the monitor. |
| `h start end pattern` | Hunt. A leading `'` or `"` takes the rest of the line as text. |
| `t` `c` | Copy or compare an inclusive range. |
| `l path [addr]` | Load a POSIX file. |
| `s path start end [fmt]` | Save. `fmt` is `S19` (the default), `IHEX`, or `BIN`. |
| `x` | Exit. `Q` also exits. |
| `?` | The command list. |

`S [n]` steps `n` decimal instructions. `B` toggles a breakpoint. `BC` clears them. `!` runs a host shell command. `IRQ`, `FIRQ`, and `NMI` assert that interrupt when the word is the whole command. There is no `N` step-over command.

`r`, startup, a step, and a finished `g` or `j` print two lines. The names are:

```
 PC   CC EFHINZVC A  B  E  F  DP X    Y    U    S    V    MD
```

`D` is `A:B` and `W` is `E:F`, so those names are not printed. `Q` is not printed either. `; D=`, `; W=`, and `; Q=` still store them. In 6809 emulation, `E`, `F`, `V`, and `MD` are dashes until an instruction or a monitor edit writes them. A dash is zero. Native 6309 mode prints the values. Writing `MD` does not enter native mode.

## Screen editing

On a terminal, the arrow keys move through the lines the monitor has drawn. A pipe or a script stays on the line reader.

Hex digits on a `>` memory line write that byte as they are typed. The ASCII column is display only. Hex digits on the `;` register line write that register as they are typed. A dash on `E`, `F`, `V`, or `MD` is zero until a hex digit is typed there. The `EFHINZVC` letters are the CC flags and are display only. Return on a `.` disassembly line assembles the mnemonic with the same assembler as `a`, and leaves the line unchanged when that assembler rejects it. Return on the blank command line runs the command.

The drawing set is the one shared by macOS Terminal, xterm, a VT100, the Linux console, and a VK100 GIGI in text mode. Cursor address is `ESC [ row ; col H`. Erase to end of line is `ESC [ K`. A VK100 ships in ANSI mode (TM1). In VT52 mode (TM0) the address is `ESC Y` with each coordinate stored as 31 plus the 1-based position, and erase-to-end-of-line is `ESC K`. Arrow keys are accepted as `ESC [ A` through `D`, `ESC O A` through `D`, and `ESC A` through `D`.

The editor does not enter ReGIS, graphics mode, or locator mode, and it does not switch to the alternate screen. `g` leaves the editor until the run stops. Ctrl-C still raises SIGINT.

`a` writes 6809 and Hitachi 6309 bytes. `LDAA #$FF` is `86 FF`. The monitor assembles and disassembles the 6309 set. The interpreter executes `SEXW`, immediate `LDQ`, direct `AIM`, and the E/F loads and adds it already implements. Immediate `LDQ` writes `Q`, which is `D:W`, so `A`, `B`, `E`, and `F` all change. `N` comes from bit 31, `Z` is set when the value is zero, and `V` is cleared. `C` and `H` stay as they were. After that write, `E` and `F` are printed as digits.

## Building

```
make
make debug
make test
```

The binary is `bin/mimic09`. `make install PREFIX=/usr/local` also installs a `mimic` symlink.

The default flags include `-D_DEFAULT_SOURCE`. Current glibc hides `cfmakeraw` and `strcasecmp` under `-std=c11` without it, and GCC 14 rejects the implicit calls. Keep that macro if you override `CFLAGS`.

From this directory, the instruction suite is:

```
../tests/run-instr-suite.sh
../tests/run-instr-suite.sh --test 6809_ABX_inh
../tests/run-instr-suite.sh --list
```

## License

mimic09 — 6809/6309 emulator and Mimic SuperMon
Copyright (C) 2026 Binary Systems, Inc.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

The full text is [`../LICENSE`](../LICENSE), the GNU GPL version 3, the same license as PROTEL.

Repository: https://gitea.binary-systems.com/binary-systems.com/Mimic.git
