# a9 — 6809 and Hitachi 6309 assembler

`a9` is a two-pass assembler. It accepts WINTEK spellings and the Motorola EXORciser / MDOS macro-assembler mnemonics and directives that this source implements, and it writes relocatable S-records. Hitachi 6309 instructions are assembled to Hitachi opcodes. The object format is [`reloc-srecords.md`](reloc-srecords.md).

This is not a full MDOS RASM. Directives that are absent from the directive table below are not implemented. A 6800 spelling emits 6809 bytes. A job whose listing shows 6800 opcodes (INX as `08`, TAB as `16`) needs a 6800 mode or a separate 6800 assembler.

## Command line

```
a9 input-files [-l listing-file] [-o output-file] [-nh]
```

Printed on stderr by `usage()`. Exit status is 1. No banner is printed. That happens when there are no arguments, no input file, or an unknown option (any argument that starts with `-` other than `-l`, `-o`, and `-nh`).

| Flag | Effect |
| --- | --- |
| input files | One or more source paths, up to 64. They are assembled as one module, pass 1 then pass 2. |
| `-l listing-file` | Write a listing. If the listing cannot be opened, a warning is printed and assembly continues. |
| `-o output-file` | Object path. The default is the first input with its extension replaced by `.rel`. |
| `-nh` | Suppress the banner and the date line. |

With at least one input file and without `-nh`, stdout begins:

```
Binary Systems, Inc. 6809 Assembler, Version 3.1r0
Copyright 2026 Binary Systems, Inc.
MM/DD/YY    HH:MM:SS
```

The date and time are local. After pass 2 the assembler always prints:

```
Assembled <first-input> -> <object> (relocatable S-record object)
```

and, when `-l` was given, `Listing: <path>`. The object file is written even when the source has errors. Errors go to stderr (`bad operand:`, `bad register:`, `branch out of range:`, `unknown mnemonic:`, `Errors detected.  Assembly aborted.`). The process exits 1 when any error was counted, and 0 otherwise.

Example, from the Mimic root:

```
cc09/bin/a9 cc09/lib/src/cstart.asm -l /tmp/cstart.lst -o /tmp/cstart.rel
cc09/bin/a9 prog.asm -nh -o prog.rel
```

## Source lines

A blank line, a line whose first non-space character is `*`, or a line whose first non-space character is `;`, is a comment.

Column 0 is a label, unless that word is a directive. `PAGE 001` and `ORG` may sit in column 0. Instructions and the other operands are indented. A line that is only a label defines that label.

`;` or `*` ends an `FCB` or `FDB` list. A space that is not the gap before the next comma also ends the list, so a remark after the last item is not assembled.

`INCLUDE` or `INCL` may quote the path with `'` or `"`. The file is opened next to the including source, then by a case-insensitive name in that directory, then under `../WINTEK` and `../lib/include` relative to the `a9` binary. A missing include aborts with `missing include:`.

## Numbers and expressions

| Form | Base |
| --- | --- |
| `$1234` | hexadecimal |
| `0x1234` | hexadecimal |
| `%1010` | binary |
| `12h` | hexadecimal when the token starts with a digit and the rest are hex digits |
| `48` | decimal |
| `'X'` or `"X"` | one character |
| `'X` | WINTEK short character, no closing quote |

There is no `@` octal prefix. `*` alone is the location counter. `+`, `-`, `*`, and `/` are accepted. `*` and `/` bind tighter than `+` and `-`. Expression results are masked to 16 bits. `LDA -1,X` stays a 5-bit indexed offset because `-1` is `0xFFFF` before the offset is encoded. A single `LDQ` immediate token (`$`, `0x`, `%`, or decimal) keeps 32 bits. `LDQ #1+1` falls back to the 16-bit expression evaluator and is stored in the low half of the immediate.

The direct page comes from `SETDP` and defaults to 0, so `$30` is direct and `$1234` is extended. `<` forces direct. `>` forces extended. `[addr]` with no comma is indirect extended, postbyte `$9F`.

## Sections

| Directive | Section id |
| --- | --- |
| `ASCT` | 0 |
| `DSCT` | 1 |
| `PSCT` | 2 |
| `BSCT` | 3 |

Ids 0 through 4 are reserved. Id 4 is unused. There is no `CSCT` directive. A label in column 0 on `ASCT`, `DSCT`, `PSCT`, or `BSCT` opens a named section. Named sections start at id 5. At most 16 sections exist in one assembly. The first instruction with no section selected opens `PSCT`.

`ORG` makes the current section absolute and emits raw S1 records when the chunk is pure literals. A named section stays relocatable S2. `END` ends the file. `NAM` is the S0 module name.

`cstart.asm` is the usual shape:

```
        TTL     'A C Start-Up Routine'
        NAM     Cstart
        GLOBAL  .CSTRT
        EXTERN  main,.clrio
        DSCT
        RMB     256
stack   EQU     *
        PSCT
.CSTRT  LDS     #stack
        ASCT
```

## Directives this assembler implements

The names are the `is_directive` table in `cc09/src/a9/a9.c`. Matching is case-insensitive.

| Directive | What the assembler does |
| --- | --- |
| `NAM` | Module name stored in the S0 record. The first word is kept. |
| `TTL` `STL` `SPC` `PAGE` | Accepted. They do not emit bytes. |
| `OPT` | `6309` selects Hitachi `CLRD`. `6809` clears that flag. `NOL` or a word starting with `NOL` turns the listing off. A word starting with `L` turns the listing on. Any other operand is accepted and changes nothing. |
| `LIST` `NOLIST` `NOL` | Listing on or off. |
| `PSCT` `DSCT` `ASCT` `BSCT` | Select that section. A column-0 label names a user section. |
| `SETDP` | Direct-page register. A value above `$FF` is shifted down a byte, so `SETDP $8400` sets page `$84`. |
| `ORG` | Absolute origin in the current section. |
| `END` | Stop this file. |
| `EQU` | Define a symbol. A second `EQU` of the same name is an error. |
| `SET` | Same value forms as `EQU`, and the symbol may be changed later. |
| `GLOBAL` | Export the names. `XDEF` is accepted and means `GLOBAL`. |
| `EXTERN` | Import the names. `XREF` is accepted and means `EXTERN`. |
| `RMB` | Advance the location counter. No bytes are stored. |
| `BSZ` | Emit that many zero bytes. |
| `FCB` | Emit each comma-separated expression as one byte. |
| `FDB` | Emit each value as a 16-bit address, relocated when the expression is relocatable. |
| `FCC` | `FCC 'text'` or `FCC "text"` emits the characters between the delimiters. `FCC 20,text` emits `count` bytes and pads with spaces. |
| `INCLUDE` `INCL` | Insert a file. |
| `MACR` `MACRO` | Start a macro. The name is the label in column 0. `ENDM` ends it. |
| `IFEQ` `IFNE` `IFGE` `IFLE` `IFGT` `IFLT` | Compare the expression with zero. `IFEQ` is true when the value is 0. `IFNE` is true when it is not 0. The signed tests use the low 16 bits as a signed number. |
| `IFC` `IFNC` | Compare the two comma-separated fields as text. |
| `ELSE` `ENDC` | Invert or close the conditional. Nesting depth is 32. |

`GLOBAL`, `EXTERN`, `FCC`, `TTL`, `STL`, `SPC`, `FCB`, and `FDB` keep the rest of the line, including spaces. Other operands are cut at the first space.

## EXORciser / MDOS names that are not implemented

The Motorola M68MASR Macro Assemblers Reference Manual (September 1979) lists these directives for the EXORciser / MDOS macro assembler: `ASCT`, `BSCT`, `BSZ`, `COMM`, `CSCT`, `DSCT`, `END`, `ENDC`, `ENDM`, `EQU`, `FAIL`, `FCB`, `FCC`, `FDB`, `IDNT`, `IFxx`, `MACR`, `NAM`, `OPT`, `ORG`, `PAGE`, `PSCT`, `REG`, `RMB`, `SET`, `SETDP`, `SPC`, `TTL`, `XDEF`, `XREF`.

`a9` does not implement `FAIL`, `COMM`, `CSCT`, `IDNT`, or `REG`. It also has no `FQB`, `ZMB`, or `FILL`. Those words are ordinary unknown mnemonics.

## Macros

The macro name is the label. The body runs until `ENDM`. A call expands when no machine instruction has that name. Parameters are `\1` through `\9`, and the WINTEK form `&\2`. `NARG` is the argument count of the expansion in progress. Nesting deeper than 4 is an error. At most 32 macros and 64 lines each are stored. A macro of the same name wins over a 6800 alias, which is how a source can define `CALL` or `PUSHU` itself.

## 6800 spellings that emit 6809 bytes

A source macro of the same name is used instead. Otherwise:

| Spelling | 6809 form |
| --- | --- |
| `CLC` `SEC` `CLI` `SEI` `CLV` `SEV` | `ANDCC` / `ORCC` with the matching mask |
| `SEIF` | `ORCC #$50` |
| `CPX` | `CMPX` |
| `PUSHU` `PUSHS` `POPU` `POPS` | `PSHU` `PSHS` `PULU` `PULS` |
| `CALL` | `JSR` |
| `ORAA` `ORAB` `LDAA` `LDAB` `STAA` `STAB` | `ORA` `ORB` `LDA` `LDB` `STA` `STB` |
| `LSLA` `LSLB` | `ASLA` `ASLB` |
| `ASLD` | `LSLD` (`10 48`) |
| `PSHA` `PSHB` `PULA` `PULB` | `PSHS A` `PSHS B` `PULS A` `PULS B` |
| `PSH A` `PSH B` `PUL A` `PUL B` | the same |
| `INX` `DEX` | `LEAX 1,X` `LEAX -1,X` |
| `INY` `DEY` | `LEAY 1,Y` `LEAY -1,Y` |
| `INS` `DES` | `LEAS 1,S` `LEAS -1,S` |
| `TSX` `TXS` `TPA` `TAP` | `TFR S,X` `TFR X,S` `TFR CC,A` `TFR A,CC` |
| `XDEF` `XREF` | `GLOBAL` `EXTERN` |
| `ADD A` `SUB A` `ADC A` `SBC A` | `ADDA` `SUBA` `ADCA` `SBCA`, and the same with `B` |

`PSHA` is the two bytes `34 02` (6809 `PSHS A`).

These names expand to more than one instruction: `TAB` is `TFR A,B` then `TSTA`; `TBA` is `TFR B,A` then `TSTA`; `CBA` is `PSHS B` then `CMPA ,S+`; `ABA` is `PSHS B` then `ADDA ,S+`; `SBA` is `PSHS B` then `SUBA ,S+`.

## Hitachi 6309

`OPT 6309` is required only for Hitachi `CLRD` (`10 4F`). Without it, `CLRD` is `CLRB` then `CLRA` (`5F 4F`), which is what `clrio.asm` uses. `OPT 6809` returns to that expansion. Pass 2 starts with the flag clear, so a `CLRD` that appears before `OPT 6309` stays `5F 4F`.

The other 6309 mnemonics do not depend on `OPT`. They use Hitachi opcodes:

`SEXW`, `LDQ`, `STQ`, `OIM`, `AIM`, `EIM`, `TIM`, `ADDR`, `ADCR`, `SUBR`, `SBCR`, `ANDR`, `ORR`, `EORR`, `CMPR`, `PSHSW`, `PULSW`, `PSHUW`, `PULUW`, `NEGD`, `COMD`, `LSRD`, `RORD`, `ASRD`, `LSLD`, `ROLD`, `DECD`, `INCD`, `TSTD`, `CLRD` (after `OPT 6309`), `COMW`, `LSRW`, `RORW`, `ROLW`, `DECW`, `INCW`, `TSTW`, `CLRW`, `SUBW`, `CMPW`, `SBCD`, `ANDD`, `BITD`, `LDW`, `STW`, `EORD`, `ADCD`, `ORD`, `ADDW`, `BAND`, `BIAND`, `BOR`, `BIOR`, `BEOR`, `BIEOR`, `LDBT`, `STBT`, `TFM`, `BITMD`, `LDMD`, `COME`, `DECE`, `INCE`, `TSTE`, `CLRE`, `COMF`, `DECF`, `INCF`, `TSTF`, `CLRF`, `SUBE`, `CMPE`, `LDE`, `STE`, `ADDE`, `DIVD`, `DIVQ`, `MULD`, `SUBF`, `CMPF`, `LDF`, `STF`, `ADDF`.

`NEGW`, `ASRW`, and `LSLW` are not in the opcode table. `CMPU`, `CMPS`, `SWI2`, and `SWI3` are 6809 instructions.

`LDQ #$12345678` is `CD 12 34 56 78` with no page prefix. Memory `LDQ` and `STQ` use prefix `10`. `OIM`, `AIM`, `EIM`, and `TIM` are opcode, immediate, then the effective address. Bit operations are direct only: `BAND A,5,1,<$40` is `11 30 69 40`. Indexed or extended bit operations emit nothing. `TFM` accepts `R+`, `R-`, `R+,R`, and `R,R+` for `D`, `X`, `Y`, `U`, and `S`. Mixed signs, `++`, `--`, and `W` emit nothing. `TFR` and `EXG` accept `W`, `V`, `E`, `F`, and `0`. Indexed offsets include `E`, `F`, and `W`.

The listing prints at most three object bytes on a line that assembled more than four, so a five-byte `LDQ` must be checked in the S-record, not only in the listing.

`a9` emits these bytes. The mimic09 interpreter executes `SEXW`, immediate `LDQ`, direct `AIM`, and the E/F loads and adds it already implements. Immediate `LDQ` writes `Q` as `D:W`, so `A`, `B`, `E`, and `F` change. `N` comes from bit 31, `Z` is set when the value is zero, and `V` is cleared. `C` and `H` are left unchanged. The interpreter does not execute the rest of this set. The monitor assembler and disassembler list the set. See [`docs/monitor-commands.md`](../../docs/monitor-commands.md).
