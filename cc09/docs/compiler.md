# xc9 — K&R C compiler, and the PROM filter

`xc9` is a reverse-engineered WINTEK K&R compiler driver. It chains `xc_1`, `xc9_3`, `a9`, and `xld` and emits 6809 object code. It is the language subset this source implements. It is not a full Lattice C front end, and it does not emit 6309 code.

The original WINTEK pass 1 had a preprocessor. Its directive names were recovered from `XC_1.EXE` and are listed below. This `xc_1` does not implement them.

## xc9 — compiler driver

```
xc9 [options] file.c ...
```

| Option | Effect |
| --- | --- |
| `-o file` | Absolute image. The default is the first source with `.c` removed and `.mik` added, in that source's directory. |
| `-map file` | Linker map. The default map is `link.map` inside the temporary directory. |
| `-list` | Keep the assembly listing next to the source, named `<stem>.lst`. |
| `-nh` | No banner. Passes `-nh` to `xc_1`, `xc9_3`, `a9`, and `xld`. |
| `-v` | Show pass banners. Passes `-v` instead of `-nh`. |
| `-Ipath` or `-I path` | Passed through to `xc_1`. This front end does not search include files. |
| other `-` options | stderr: `warning: option %s not used by this driver`. The driver continues. |

Without `-nh`, stdout begins:

```
Binary Systems, Inc. Compiler Driver, Version 1.0 r3
Copyright 2026 Binary Systems, Inc.
```

No source file prints `error on file: no C source files` on stderr, then the usage text, and exits 1. The banner has already been printed unless `-nh` was set. More than 16 sources abort.

For each `.c` file the driver runs, from the directory that contains the `xc9` binary:

```
xc_1 <work>/<stem>.icf1 <work>/<stem>.icf2 <source> -nh
xc9_3 <work>/<stem>.icf1 <work>/<stem>.icf2 <work>/<stem>.asm -nh
a9 <work>/<stem>.asm -nh -o <work>/<stem>.rel
```

`-v` replaces each `-nh`. `-list` adds `-l <source-dir>/<stem>.lst` to `a9`. `<work>` is `/tmp/mimic-xc9-<pid>`.

It then assembles `cstart.asm`, `chario.asm`, `clrio.asm`, and `stdio.asm` from `../lib/src` relative to the `xc9` binary, writes `default.dir` (in the work directory and in the current directory), and links:

```
xld -nh -d <work>/default.dir -o <image> -map <map> <user rels> cstart.rel chario.rel clrio.rel stdio.rel
```

`default.dir` names `rnlib9.lib`. That file ships at `cc09/lib/rnlib9.lib`. `xld` looks for a library in the current directory, then in `../lib` next to the `xld` binary, then in a local `../WINTEK` checkout. A pass that exits non-zero stops the driver with that status. After the link, a map symbol under `UNSATISFIED EXTERNAL REFERENCES` prints `xc9: unsatisfied external <name>` and the driver exits 1. Success prints `xc9: <image>` and exits 0.

The original `XC9.EXE` option list, recovered from the binary, is `-ns`, `-S`, `-ni`, `-c`, `-v`, `-list`, `-al`, `-nh`, `-np`, and `-I`, with file suffixes `.c`, `.xc`, `.asm`, `.rel`, `.lib`, and `.dir`. This driver implements the options in the table above. The original names it does not implement produce the warning and are otherwise ignored.

Example:

```
cc09/bin/xc9 -o tmp/c-hello.mik -map tmp/c-hello.map tests/c/hello.c
```

## xc_1 — pass 1

```
xc_1 icfile1 icfile2 srcfile [-v] [-nh] [-Ipath]
```

Three positional files are required, in that order. `-v` and `-nh` are flags. `-Ipath` and a separate `-I path` are accepted and not used. Any other argument that starts with `-` prints `Unknown command line option`, then the usage line, and exits 1.

Without `-nh`, stdout begins:

```
Binary Systems, Inc. C Compiler, Pass 1, Version 1.0 r3
Copyright 2026 Binary Systems, Inc.
```

Missing files print the banner (unless `-nh`) and then the usage line on stderr, and exit 1. A source that cannot be opened prints `can't open input file` and exits 1. A parse error prints ` Errors detected.  Compilation aborted.` (leading space) and exits 1. A clean pass prints ` End pass 1.  No errors detected.` when the banner is on, or when `-v` is set even with `-nh`.

`icfile1` is the call tree (`FUNC`, `CALL`, `ARG`, `RET`, `ENDFUNC`). `icfile2` is the string pool, one line of hex bytes per string.

### Language this pass accepts

A first line that starts with `#!` is skipped. `//` and `/* */` comments are skipped. An unclosed comment is `comment not terminated`.

A translation unit is a sequence of functions:

```
int main(void)
{
    printf("Hello, world\r\n");
    return 0;
}
```

`int`, `void`, and `char` are type keywords on the function and on its parameters. Each parameter occupies one 16-bit stack slot. The body is a brace block of calls and `return`. A call argument is a string or an integer (`123` or `0x7B`, with an optional leading minus). `return;` and `return 0;` are accepted. A richer expression is `expression too complex`. An undeclared call is left as an external, which is the K&R implicit-int rule this subset keeps.

String escapes are `\n`, `\r`, `\t`, `\b`, `\f`, `\\`, `\'`, `\"`, and up to three octal digits. Any other escape prints `WARNING - unknown character escape sequence` and keeps the character.

### Preprocessor directives recovered from XC_1.EXE

The original pass 1 string table names these preprocessor directives: `define`, `undef`, `include`, `line`, `if`, `ifdef`, `ifndef`, `else`, `endif`, `debug`, `list`. The same binary reports `unknown preprocessor directive`, `malformed preprocessor directive`, `can't open include file`, and nested `#include` / `#if` errors.

This `xc_1` implements none of those directives. A `#` that is not the shebang on the first line is an ordinary character and will fail the parse.

## xc9_3 — 6809 code generator

```
xc9_3 icfile1 icfile2 asmfile [-v] [-ns] [-ni] [-nh] [-np] [-al]
```

Three positional files are required. `-nh` suppresses the banner. `-v` requests the summary line. `-ns`, `-ni`, `-np`, and `-al` are accepted and do not change the assembly this generator writes. Any other option prints `unknown option:` and the usage line, and exits 1.

Without `-nh`, stdout begins:

```
Binary Systems, Inc. 6809 Code Generator, Version 1.0 r3
Copyright 2026 Binary Systems, Inc.
```

A missing intermediate file aborts. A finished pass prints ` End code-gen.      N program bytes generated.` when the banner is on, or when `-v` is set.

The assembly uses the calling convention in [Argument passing](#argument-passing). `main(void)` is entered from `cstart` with `A` equal to 0.

The generator's own assembler directives are `NAM`, `GLOBAL`, `EXTERN` (always `.LINK`, plus each call that is not defined in this file), `PSCT`, `FCB` for string bytes, and `END`. Strings are labeled `S0`, `S1`, and so on, and are terminated by a zero byte.

## Argument passing

C and assembly use one stack convention. The hardware stack `S` carries the arguments, the return address, and the return value's cleanup. `U` is not an argument pointer. `cstart` points `S` at the end of a 256-byte `DSCT` block (`RMB 256`, then `LDS #stack`), and calls grow down into that block.

Every C argument is one 16-bit word, including `char`. The word is stored big-endian: the high byte is at the lower address. `PSHS D` pushes `B` and then `A`, so `A` is at `S` and `B` is at `1,S`. `JSR` and `BSR` then push the return address the same way, high byte at `S`.

The caller pushes arguments left to right and puts the byte count in `A`. Two arguments are 4, one argument is 2, and no arguments is 0. `B` is not part of the count. The caller does not remove the arguments. The callee does.

`printf("Hi\r\n")` is compiled as:

```
        LDX     #S0       ; address of the NUL-terminated string
        PSHS    X
        LDA     #2
        JSR     printf
```

`putc(c, fp)` with both arguments integers is:

```
        LDD     #c
        PSHS    D         ; first argument, deepest
        LDD     #fp
        PSHS    D         ; second argument, next to the return address
        LDA     #4
        JSR     putc
```

On entry to the callee, after `JSR` has pushed the return address, `S` addresses this frame. Offsets increase toward older bytes.

| Offset | Bytes | Contents |
| --- | --- | --- |
| `0,S` | 2 | return address |
| `2,S` | 2 | last argument (rightmost) |
| `4,S` | 2 | argument before that |
| `2+2*(n-1),S` | 2 | first argument (leftmost) |

For one argument the high byte is at `2,S` and the low byte is at `3,S`. `putchar` reads the character with `LDB 3,S`. For `putc(c, fp)` the character is the first argument, so it is at `4,S`, and the file pointer is at `2,S`. `chario.asm` loads that character with `LDD 4,S`.

A C function generated by `xc9_3`, and a hand-written assembly function called from C, begins with the byte count it expects:

```
putchar SUBA    #2
        BEQ     putchr0
        JSR     .LINK
putchr0 ...
```

`SUBA` leaves `Z` set and `C` clear when `A` held exactly that count, and the body is entered at the label. A different count calls `.LINK` in `rnlib9.lib`. That routine repairs the stack and returns to the body with `Z` set and `C` clear:

- More bytes than expected: the extra rightmost bytes are discarded. The leftmost arguments stay.
- Fewer bytes than expected: the missing rightmost arguments are filled with zero. The arguments that were passed stay in the leftmost slots.

After `.LINK` returns, the body sees the same frame as a matching call. `getchar` and `cleario` expect 0. `putchar` and `getc` expect 2. `putc` expects 4.

The return value is the 16-bit word in `D` (`A` high, `B` low). The runtime routines also leave `Z` set when that word is zero (`CLRD`, `TSTB`, or `SUBD #0`). Generated code returns a constant with `LDD #value`, or zero with `CLRA` / `CLRB`.

A function with no parameters returns with `RTS`. The arguments are already absent, so the return address is all that `RTS` pops. `getchar`, `cleario`, and a `main(void)` generated by `xc9_3` do this.

A function with parameters must remove them. `RTS` alone would return to the caller and leave the argument bytes allocated. The generated exit, and `getc` in `chario.asm`, is:

```
        PULS    X         ; return address
        LEAS    n,S       ; n is the parameter byte count
        JMP     0,X
```

`putchar` pops the one argument into `D` and returns that word:

```
        PULS    X
        PULS    D
        SUBD    #0        ; Z reflects the value
        JMP     0,X
```

### C calling assembly

Give the assembly function the C name, a `GLOBAL`, and the entry test above. Read each argument from the frame, put the result in `D`, and pop the frame when the count is not zero.

`putchar` is the one-argument case. `chario.asm` documents it as `putchar(c)` with `c` a character. The low byte at `3,S` is that character. The matching call from C or from assembly is:

```
        CLRA
        LDB     char
        PSHS    D
        LDA     #2
        JSR     putchar
        ; D is the character just written, and the argument is gone
```

`INCH` and `OUTCH` in the same file are ordinary assembly routines. They are reached with `BSR`, they pass the character in `B`, and they return with `RTS`. They do not consult `A` or the C frame. Call those names from assembly when you do not want the C protocol. Call `putchar` and `getchar` when the other side is C.

`cleario` is the C entry (`cleario()`, count 0). `.clrio` is the assembly entry: `JSR .clrio` with no count and no stack arguments. `cstart` uses the assembly entry to reset the ACIA before it calls `main`.

### Assembly calling C

Set up the same frame the compiler emits, then `JSR` the C name. The C function removes its arguments and returns the result in `D`.

`cstart.asm` calls `main(void)`:

```
        CLRA              ; byte count 0
        JSR     main
        ; main's RTS comes back here; D holds main's return value
        JMP     .CSTRT    ; cstart ignores D and starts main again
```

`putc` in `chario.asm` calls the C function `putchar` with the first of its own two arguments:

```
        LDD     4,S       ; leftmost argument, the character
        PSHS    D
        LDA     #2
        BSR     putchar   ; BSR pushes a return address, same as JSR
        PULS    X
        LEAS    4,S       ; drop putc's own two arguments
        JMP     0,X
```

A call from assembly to a two-argument C function looks like the `putc(c, fp)` sequence above. Declare the C name `EXTERN` in the assembly file and `GLOBAL` from the C side (the compiler emits that `GLOBAL` for every function it compiles). Link both objects with `xld` so the call resolves.

## hex — PROM image filter

```
hex [options] first last <input >output
```

`first` and `last` are hexadecimal. A leading `$` is accepted. The window is inclusive. A missing or backwards window prints `invalid address range` and then the usage text, and exits 1.

The banner and the usage text go to stderr:

```
     PROM Image Filter, V1.0r3
Copyright 2026 Binary Systems, Inc.
Usage: hex [options] first last <input >output
Available options are:
  -W for Wintek PROM pinout
  -A for inverting address buffers
  -D for inverting data buffers
  -S for S-record format output
```

The first banner line keeps its five leading spaces. The `-W` line is the PROM pinout message and stays worded that way.

| Option | Effect |
| --- | --- |
| `-W` | Accepted. The bytes are unchanged. |
| `-A` | Accepted. The bytes are unchanged. |
| `-D` | Invert each data byte. |
| `-S` | Write S1 records and end with `S9030000FC`. Without `-S`, the output is Intel hex type 00 records of up to 16 data bytes, CRLF, and the file ends with `:00000001FF`. |

Input is stdin. S1, S2, and S3 records and Intel hex are loaded into a 64K image. S0, S5, S9, and SD are ignored. An address in the window that was never loaded is `0xFF`. A bad checksum prints `checksum error` on stderr. Success prints the image on stdout and exits 0. The banner is also printed on success, on stderr, so a redirected image does not contain it.

```
cc09/bin/hex -S C000 FFFF < image.mik > prom.s19
```

## mimic-cc09 — shebang driver

`bin/mimic-cc09` is a shell script, installed by the top-level `make`. A source file can start with:

```
#!/usr/bin/env mimic-cc09
```

```
mimic-cc09 source.asm
mimic-cc09 source.c
```

No file, or a missing file, prints `mimic-cc09: usage: mimic-cc09 source.[asm|c]  (or use as shebang interpreter for such files)` on stderr and exits 1. Suffixes `.asm` and `.s` are assembly. `.c` is C. Anything else exits 1.

Assembly runs `a9` to a temporary `.rel`, then `xld` to a temporary `.mik`. The link status is ignored (`xld` is followed by `|| true`). C runs `xc9 -o <mik> -map <map> <source>` and exits 1 if `xc9` is missing or fails.

The image is loaded with:

```
mimic09 -l <image> --acia 0xF940 -g
```

The emulator is stopped after one second. Its output is copied to the terminal between the two marker lines `================ program I/O ================` and `================ end program I/O ================`. The script then exits 0.

`PATH` must include `bin/` for a `#!/usr/bin/env mimic-cc09` file. `tests/c/hello.c` is that program. `make hello` from the Mimic root compiles it with `xc9` and runs it under `mimic09` directly, without the one-second shebang cutoff.
