# Mimic

Mimic is a 6809 and Hitachi 6309 toolchain and emulator for macOS and UNIX. `cc09` recreates the WINTEK assembler, relocatable linker, and K&R C compiler. `mimic09` is the CPU emulator and Mimic SuperMon.

```
make                  # build cc09, mimic09, and bin/mimic-cc09
make test             # assembler, linker, compiler smoke, and the CPU suite
make hello            # compile tests/c/hello.c and run it on the ACIA
```

Binaries land in `cc09/bin/` and `mimic09/bin/`. The shebang driver is `bin/mimic-cc09`.

`make` uses `cc` and GNU make on macOS and Linux. The makefiles already pass the feature-test macros current glibc needs: `-D_DEFAULT_SOURCE` and `-D_POSIX_C_SOURCE=200809L` for `cc09` (`readlink`, `realpath`, `strcasecmp`), and `-D_DEFAULT_SOURCE` for `mimic09` so `-std=c11` still declares `cfmakeraw` and `strcasecmp`. Keep those macros if you set `CFLAGS` yourself.

`make hello` compiles `tests/c/hello.c` and links `cc09/lib/rnlib9.lib` from this tree. That library supplies `printf` and `.LINK`.

## License

Mimic — 6809/6309 toolchain and emulator
Copyright (C) 2026 Binary Systems, Inc.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

The full license text is [`LICENSE`](LICENSE). It is the GNU GPL version 3, the same license as PROTEL.

## Command lines

Each line below is the usage text the program prints. Banners, flags, and exit status are in the linked pages.

| Program | Usage |
| --- | --- |
| [`cc09/bin/a9`](cc09/docs/assembler.md) | `a9 input-files [-l listing-file] [-o output-file] [-nh]` |
| [`cc09/bin/xld`](cc09/docs/linker.md) | `xld [-v] [-nh] [-map mapfile] [userfile ...] [-d dirfile] [-l libfile ...] [-o absfile]` |
| [`cc09/bin/xc9`](cc09/docs/compiler.md) | `xc9 [options] file.c ...` |
| [`cc09/bin/xc_1`](cc09/docs/compiler.md) | `xc_1 icfile1 icfile2 srcfile [-v] [-nh] [-Ipath]` |
| [`cc09/bin/xc9_3`](cc09/docs/compiler.md) | `xc9_3 icfile1 icfile2 asmfile [-v] [-ns] [-ni] [-nh] [-np] [-al]` |
| [`cc09/bin/hex`](cc09/docs/compiler.md) | `hex [options] first last <input >output` |
| [`mimic09/bin/mimic09`](mimic09/README.md) | `mimic09 [options] [file]` |
| [`bin/mimic-cc09`](cc09/docs/compiler.md) | `mimic-cc09 source.asm` or `mimic-cc09 source.c` |

`a9` writes relocatable S-records. `xld` links them to an absolute image. `xc9` runs `xc_1`, `xc9_3`, `a9`, and `xld` and emits 6809 object code. C and assembly pass arguments on the stack as described in [Argument passing](cc09/docs/compiler.md#argument-passing). `mimic09` loads that image. On a terminal, Mimic SuperMon arrow keys edit the memory and register lines it has drawn. The command reference is [`docs/monitor-commands.md`](docs/monitor-commands.md).

## Layout

- `cc09/` assembler, linker, C compiler, and PROM filter. See [`cc09/README.md`](cc09/README.md).
- `mimic09/` emulator and Mimic SuperMon. See [`mimic09/README.md`](mimic09/README.md).
- `docs/monitor-commands.md` SuperMon commands and screen editing.
- `tests/` instruction suite, `tests/c/hello.c`, and `tests/asm/acia-hello.asm`.
