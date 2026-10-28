# xld — relocatable linker

`xld` reads `a9` relocatable S-records, places the sections, applies the fixups, and writes an absolute S-record image plus an optional map. The record layout is [`reloc-srecords.md`](reloc-srecords.md).

## Command line

```
xld [-v] [-nh] [-map mapfile] [userfile ...] [-d dirfile] [-l libfile ...] [-o absfile]
```

An unknown option (an argument that starts with `-` and is not one of the flags above) prints that line on stderr and exits 1. The banner is not printed in that case.

| Flag | Effect |
| --- | --- |
| `-v` | After a successful link, print `         output file: <path>` and `entry XXXX` on stdout. |
| `-nh` | Suppress the banner. The "Link complete" lines are also suppressed unless `-v` is set. |
| `-map mapfile` | Write the map. |
| user files | Relocatable objects. Each one is linked. |
| `-d dirfile` | Read a directive file. Its `FILE` lines are further objects. Its `LIBRARY` lines are libraries. |
| `-l libfile` | A library, searched for unsatisfied externals. Repeatable. |
| `-o absfile` | Absolute image. The default name is `a.mik`. |

Without `-nh`, stdout begins:

```
Binary Systems, Inc. Linker, Version 2.1r0
Copyright 2026 Binary Systems, Inc.
```

If there is no user file and the directive file names no `FILE`, stderr gets `no input files` and the process exits 1. The banner has already been printed unless `-nh` was set.

A finished link prints, unless `-nh` was set without `-v`:

```
Link complete -> <image> (absolute S-record)
Map: <mapfile>
```

The map line is printed only when `-map` was given. Exit status is 0. A missing object, a bad checksum, or a map that cannot be opened aborts with status 1.

Example:

```
cc09/bin/xld -nh prog.rel -o prog.mik -map prog.map
cc09/bin/xld -d app.dir -l rnlib9.lib -o app.mik -map app.map
```

Libraries are opened from the path as given, from the file name in the current directory (case-insensitive), from the directory that holds the directive file, and, for a library, from `../lib` and then `../WINTEK` relative to the `xld` binary. `cc09/lib/rnlib9.lib` is the runtime `printf` and `.LINK` library. A `../WINTEK` directory is only a local checkout of the original archive.

## Directive file

Blank lines and lines whose first non-space character is `*` or `;` are comments. Names are case-insensitive. `END` stops the file. Anything else that is not one of the directives below is skipped, including a bare address span such as `0-DFFF`.

| Directive | Meaning |
| --- | --- |
| `FILE path` | Another relocatable object. |
| `LIBRARY path` | A library. Modules are pulled while they satisfy an external. |
| `SECTION type @ lo-hi` | Place that builtin section in the hex window. `type` is `ASCT`, `DSCT`, `PSCT`, `BSCT`, or `CSCT`. |
| `SECTION name type @ lo-hi` | Place the named section. The type word is recognized and is not part of the name. The window is stored under `name`. |
| `SECTION name @ lo-hi` | Place the named section when `name` is not a builtin type. |
| `END` | Stop reading. |

Hex spans accept an optional `$`. `DFFE-DFFF` and `$DFFE-$DFFF` are the same. A single number runs through `$FFFF`.

The original `XLD.EXE` also recognizes `MODULE`, `QUIT`, and `DEFINE`. This linker does not. Those lines are skipped.

`xc9` writes this default file, both under its temporary directory and as `default.dir` in the current directory:

```
*  Default linker directives
LIBRARY rnlib9.lib
0-DFFF
SECTION * BSCT @ 0-FF
END
```

`0-DFFF` is skipped. `SECTION * BSCT @ 0-FF` is the named form, with the name `*`.

## Placement and the image

With no `SECTION` directive, relative contributions are packed in S4 order. The cursor starts at `$0100`. A `BSCT` of at most 256 bytes is placed at 0 and does not move the cursor. Absolute chunks are placed at their `ORG`.

The entry point is the word at `$FFFE` when that word points into a `PSCT` contribution. Otherwise it is the first `PSCT` global. An unsatisfied external is written as 0.

The image is S1 records of at most 32 data bytes, then a linked `SD` when the object had an `SD`, then `S9` at address 0. The map starts with a blank line, then:

```
Binary Systems, Inc. Linker, Version 2.1r0
Copyright 2026 Binary Systems, Inc.
```

and then the section sizes, `Entry Point at:`, and `UNSATISFIED EXTERNAL REFERENCES`.
