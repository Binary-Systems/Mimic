# cc09 — 6809 assembler, linker, and C compiler

`a9` assembles WINTEK and EXORciser / MDOS 6809 source, including the Hitachi 6309 mnemonics this assembler implements, and writes relocatable S-records. `xld` links those objects to an absolute image. `xc9` is the K&R C driver: `xc_1`, `xc9_3`, `a9`, and `xld` produce 6809 object code. `hex` is the PROM image filter.

| Program | Usage | Detail |
| --- | --- | --- |
| `bin/a9` | `a9 input-files [-l listing-file] [-o output-file] [-nh]` | [docs/assembler.md](docs/assembler.md) |
| `bin/xld` | `xld [-v] [-nh] [-map mapfile] [userfile ...] [-d dirfile] [-l libfile ...] [-o absfile]` | [docs/linker.md](docs/linker.md) |
| `bin/xc9` | `xc9 [options] file.c ...` | [docs/compiler.md](docs/compiler.md) |
| `bin/xc_1` | `xc_1 icfile1 icfile2 srcfile [-v] [-nh] [-Ipath]` | [docs/compiler.md](docs/compiler.md) |
| `bin/xc9_3` | `xc9_3 icfile1 icfile2 asmfile [-v] [-ns] [-ni] [-nh] [-np] [-al]` | [docs/compiler.md](docs/compiler.md) |
| `bin/hex` | `hex [options] first last <input >output` | [docs/compiler.md](docs/compiler.md) |

Without `-nh`, each compiler and assembler banner names Binary Systems, Inc. and prints `Copyright 2026 Binary Systems, Inc.` `hex` prints `     PROM Image Filter, V1.0r3` and the same copyright on stderr. Its usage line `-W for Wintek PROM pinout` is unchanged. `-W` and `-A` do not alter the image bytes.

The object format is [docs/reloc-srecords.md](docs/reloc-srecords.md).

```
cc09/bin/a9 cc09/lib/src/cstart.asm -l /tmp/cstart.lst -o /tmp/cstart.rel
cc09/bin/xld /tmp/cstart.rel -o /tmp/cstart.mik -map /tmp/cstart.map
cc09/bin/xc9 -o tmp/hello.mik -map tmp/hello.map tests/c/hello.c
cc09/bin/hex -S C000 FFFF < image.mik > prom.s19
```

Run those from the Mimic root, or run `bin/a9` from this directory.

## Building

```
make
make test
make asm-test
make link-test
make legacy-smoke
```

`make install PREFIX=/usr/local` installs the binaries. The top-level Mimic `make` also builds `mimic09` and refreshes `bin/mimic-cc09`.

The default `CFLAGS` include `-D_DEFAULT_SOURCE` and `-D_POSIX_C_SOURCE=200809L`. Current glibc hides `readlink`, `realpath`, and `strcasecmp` without them, and a strict compiler then rejects the calls. Keep both macros if you override `CFLAGS`.

`lib/rnlib9.lib` is the runtime library `xc9` links for `printf` and `.LINK`. It is a text S-record file with CR/LF endings. Leave those bytes unchanged.

## What the tools cover

`a9` implements the directive table in [docs/assembler.md](docs/assembler.md): sections `PSCT`, `DSCT`, `ASCT`, and `BSCT`, `NAM`, `GLOBAL` / `XDEF`, `EXTERN` / `XREF`, `FCB`, `FDB`, `FCC`, `RMB`, `EQU`, `SET`, macros, and the `IF` / `ELSE` / `ENDC` forms in that table. `FAIL`, `COMM`, `CSCT`, `IDNT`, and `REG` are not implemented. 6800 spellings such as `LDAA` and `PSHA` assemble to 6809 bytes. `PSHA` is `34 02`. `CLRD` is `CLRB` then `CLRA` unless the source says `OPT 6309`.

`xld` places relative sections from `$0100`, with a short `BSCT` at 0, and honors `FILE`, `LIBRARY`, `SECTION`, and `END`. `MODULE`, `QUIT`, and `DEFINE` are not implemented.

`xc9` compiles the subset `hello.c` needs: a shebang line, comments, `int` / `void` / `char` functions, string and integer arguments, calls, and `return`. The original pass 1 preprocessor (`#define`, `#include`, `#if`, and the rest listed in the compiler page) is not implemented. Calls pass 16-bit arguments on `S`, left to right, with the byte count in `A`. The callee removes them and returns the result in `D`. The same frame is how C calls assembly and how assembly calls C. See [Argument passing](docs/compiler.md#argument-passing). `printf` and `.LINK` come from `lib/rnlib9.lib`, which is part of this tree.

## Original archive

The original WINTEK tools are archived at https://gitea.binary-systems.com/binary-systems.com/WINTEK.git . They are not part of Mimic. A local checkout may sit in `WINTEK/` and is gitignored. Do not edit anything under `WINTEK/`. Do not push that archive to GitHub.

## License

cc09 — 6809 assembler, linker, and C compiler
Copyright (C) 2026 Binary Systems, Inc.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

The full text is [`../LICENSE`](../LICENSE), the GNU GPL version 3, the same license as PROTEL.
