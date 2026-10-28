# Mimic SuperMon

mimic09's monitor. There is no prompt character. The name is Mimic SuperMon. Help is `?`. `H` is hunt.

The first letter of a command is case-insensitive. The rest of the line is left as typed, so a path keeps its case. A prefix sets the radix of any operand: `$` hex, `+` decimal, `&` octal, `%` binary, `0x` hex. No prefix means hex.

A line that is only a number prints that value in all four forms. `$2000` prints `$2000`, `+8192`, `&20000`, and `%10000000000000`.

## Commands

| Command | What it does |
| --- | --- |
| `r` | Print the register names, then one `;` value line. |
| `;` | With `=`, set named registers (`PC`, `CC`, `A`, `B`, `D`, `E`, `F`, `W`, `Q`, `DP`, `X`, `Y`, `U`, `S`, `V`, `MD`). Without `=`, eight numbers are PC, CC, A, B, X, Y, U, S. The printed value line, including the flag letters, sets the full line. A field of dashes is zero and does not count as a write. |
| `m [start [end]]` | Eight-byte hex and ASCII lines, each starting with `>`. One address, or none, shows 12 lines. A range is inclusive and rounds up to a whole line. |
| `>addr bytes` | Write bytes onto that line. A `:` stops the parse so the ASCII tail is left alone. |
| `d [start [end]]` | Disassemble. Lines start with `.`. Opcode bytes use five columns, so the mnemonic starts at the same column on every line. One address shows 20 bytes. Two addresses run from start through end. No address continues. |
| `a addr mnem` | Assemble one 6809 or Hitachi 6309 instruction, print the bytes, and return to the monitor. |
| `a addr` | Prompt `a ADDR` until a blank line. |
| `f start end value` | Fill the inclusive range. |
| `g [addr]` | Run from that address, or from PC. Stops on halt, a breakpoint, or Ctrl-C. |
| `j addr` | Push a return and call that address. `RTS` comes back to the monitor. Refuses when S is below 2. |
| `h start end 'text` | Hunt the inclusive range. A leading `'` or `"` takes the rest of the line as text. Otherwise the pattern is bytes, at most 32. Prints each match. |
| `t start end dest` | Copy the inclusive range. Overlap is safe. |
| `c start end dest` | Compare the inclusive range. Prints each source address that differs. |
| `l path [addr]` | Load a POSIX file. S-records and Intel hex are detected. An address forces a raw load. |
| `s path start end [fmt]` | Save an inclusive POSIX file. `fmt` is `S19` (the default), `IHEX`, or `BIN`. |
| `s "name",dev,start,end` | Quoted form. The device field is ignored. `end` is exclusive. |
| `x` | Exit. |
| `@` | One-line reject. Host files are `l` and `s`. |
| `?` | This command list. |

## Mimic extras

These stay so the instruction suite and the existing debugger keep working. SuperMon has no `BC`.

| Command | What it does |
| --- | --- |
| `S [n]` | Step `n` instructions. `n` is decimal. An empty argument steps once. A path, slash, dot, or quote is a save, not a step. |
| `B [addr]` | Toggle a breakpoint. No address lists them. |
| `BC` | Clear every breakpoint. |
| `! command` | Run a host shell command. |
| `:addr bytes` | Poke bytes. The instruction suite uses this. |
| `Q` | Exit, same as `X`. |
| `IRQ` `FIRQ` `NMI` | Assert that interrupt. The word must be the whole command. |

## Registers

`r`, startup, step, and a finished `g` or `j` print two lines. The first is the names. The second starts with `;` and is the values:

`PC CC EFHINZVC A B E F DP X Y U S V MD`

`D` is `A:B` and `W` is `E:F`, so those names are not printed. `Q` is `D:W` and is not printed either. `; D=`, `; W=`, and `; Q=` still store them. `EFHINZVC` shows the CC bits and is not its own register.

In emulation mode, `E`, `F`, `V`, and `MD` are dashes until an instruction or a monitor edit writes them. Native 6309 mode prints the values instead. A dash is zero. The cycle counter is not on the line.

## Screen editing

On a terminal, the arrow keys move through the lines the monitor has drawn. A pipe or a script stays on the line reader, so the instruction suite is unchanged.

Hex digits on a `>` memory line write that byte as they are typed. The ASCII column is display only. Hex digits on the `;` register line write that register as they are typed. A dash on `E`, `F`, `V`, or `MD` is zero until a hex digit is typed there. The `EFHINZVC` letters are the CC flags and are display only. Return on a `.` disassembly line assembles the mnemonic with the same assembler as `a`, and leaves the line unchanged when that assembler rejects it. Return on the blank command line runs the command.

The drawing set is the one shared by macOS Terminal, xterm, a VT100, the Linux console, and a VK100 GIGI in text mode. Cursor address is `ESC [ row ; col H`. Erase to end of line is `ESC [ K`. A VK100 ships in ANSI mode (TM1). In VT52 mode (TM0) the address is `ESC Y` with each coordinate stored as 31 plus the 1-based position, and erase-to-end-of-line is `ESC K`. Arrow keys are accepted in all three forms: `ESC [ A` through `D`, `ESC O A` through `D`, and `ESC A` through `D`.

The editor does not enter ReGIS, graphics mode, or locator mode, and it does not switch to the alternate screen. A window narrower than a full dump shortens the memory line. One address still shows 12 lines when there is room. Lines that scroll off the top are no longer editable. Output from `!` stays on the glass when the terminal answers a cursor-position report; a VT52 session redraws the monitor buffer instead. `g` leaves the editor until the run stops. Ctrl-C still raises SIGINT.

## Assembler

`a` writes 6809 and Hitachi 6309 bytes. The 6809 subset covers inherent opcodes, immediate, direct, extended, relative, and indexed (`n,X`, `,X`, `A,X`, `B,X`, `D,X`, `E,X`, `F,X`, `W,X`, and the same bases Y, U, and S). `<` forces direct. `>` forces extended. A short address with both forms is direct, so `lda $30` is `96 30` and `lda $2000` is `B6 20 00`.

`LDAA`, `LDAB`, `STAA`, `STAB`, `ORAA`, `ORAB`, `LSLA`, and `LSLB` are 6809 spellings. `LDAA #$FF` is `86 FF`. `ASLD` assembles as `LSLD` (`10 48`). This assembler does not emit 6800 opcodes.

6309 mnemonics use Hitachi opcodes. The set is `SEXW`, `LDQ`/`STQ`, `OIM`/`AIM`/`EIM`/`TIM`, `ADDR` through `CMPR`, `TFR`/`EXG` (including `W`, `V`, `E`, and `F`), `PSHSW`/`PULSW`/`PSHUW`/`PULUW`, the D and W inherent ops, W and Q memory ops, `BAND` through `STBT`, `TFM`, `BITMD`/`LDMD`, `SWI3`, the E and F ops, and `DIVD`/`DIVQ`/`MULD`. `LDQ #$12345678` is `CD 12 34 56 78`. `TFM X+,Y+` is `11 38 12`. `BAND A,5,1,<$40` is `11 30 69 40`.

The monitor assembles and disassembles that set. The interpreter executes `SEXW`, immediate `LDQ`, direct `AIM`, and the E/F loads and adds it already implemented. Immediate `LDQ` writes `Q` as `D:W`, so `A`, `B`, `E`, and `F` change. `N` comes from bit 31, `Z` is set when the value is zero, and `V` is cleared. `C` and `H` are left unchanged. That write prints `E` and `F` as digits. `TFM`, the bit ops, `DIVD`, `DIVQ`, `MULD`, and the inter-register ops are listed by the assembler and disassembler.

## Load and save

Paths are POSIX. The scripted session uses `l` and `s` with a filesystem path. `@` is not a cassette command.
