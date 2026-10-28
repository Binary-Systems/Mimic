# Relocatable S-Records (Wintek a9 / xld)

Checked against `tests/golden/cstart_real.rel`, `chario_real.rel`, and `cstart_real.mik`. Checksums are the usual one's complement of the count, address, and data bytes. Object files use CRLF. `srec_emit` is the writer (address length 2, except types 2, 7, and 8 which are 24-bit).

## Address field

| Type | Address width | Meaning |
| --- | --- | --- |
| S0, S1, S4, S9, SD | 16-bit | S0 and object S9 are address 0. S4 address 0 means a relative section; a non-zero S4 address is an absolute org. |
| S2, S7, S8 | 24-bit | Packed as `(offset << 8) \| section`. |

Section ids in this field are ASCT=0, DSCT=1, PSCT=2, BSCT=3. Those are not the type numbers in `sections.c`.

## Records a9 emits

- S0 address 0. Payload is the `NAM` bytes.
- S8 when a `GLOBAL` label is defined. Data is `00 00` plus the name. The address offset is the label's section offset.
- S7 for an external. Data is the name only. The address offset is the 16-bit fixup PC. The S7 is written immediately before the S2 chunk that starts with that external.
- S4 when a section is closed and its size is greater than 0. Address 0: data is section, inclusive end high, inclusive end low (size = end + 1). Address not 0: the section is absolute; data is the section id plus the absolute end. An ASCT that holds only `EQU`s (such as `ioloc.h`) emits no S4.
- S2 chunks. The 24-bit address offset is the section PC where the chunk's code begins. An absolute section's offset is the absolute address.
- Object SD only if some section other than PSCT was emitted. Address 0, data `02 08`. `chario` is PSCT-only and has no SD. `cstart` has `SD0500000208F0`.
- S9 address 0.

## Chunk bytes

A chunk is a token stream split at each external. The opcode literal stays in the previous chunk. The new chunk starts at the fixup PC.

- `C0 ah al` opens an external fixup. It occupies a 2-byte address hole and is not stored as zeros. A following literal starts at PC+2.
- `C1 sec disp16` is another section. `disp` is `symbol_offset - section_size` as a signed 16-bit value. The linker address is `base(sec) + size(sec) + disp`. A PSCT symbol uses `C2` plus the 16-bit section offset instead. `C1` is 4 object bytes and 2 PC bytes. `C2` is 3 object bytes and 2 PC bytes.
- An absolute `EQU` is literal bytes. A branch offset is computed by the assembler.

Encoding of one S2 payload (33 data bytes maximum):

- Chunk starts with an external: `C0`, then a shield length, then that many leading literal tokens, then the raw tail. The shield excludes a dangling trailing literal when the chunk was closed by the next external and there is no real reloc, and it excludes the opcode literal immediately before the first real `C1`/`C2`. An empty body is `C0 00 00`.
- First token is `C1` or `C2`: the tokens are raw. There is no count prefix.
- Real reloc that does not lead with an external: the first byte is the reloc-token count, then every token.
- Pure literals: the first byte is `ncode - 1`, then every code byte. The decoder drops that first byte.

A `C0` payload longer than 33 bytes peels 29 leading literal tokens into the first record. The remainder is a new chunk. `chario` `putc`/`OUTCH` at offset `004E` splits this way; the next chunk is at `006D`.

Decoder: if the data starts with `C0`/`C1`/`C2`, scan from byte 0. Otherwise if any `Cx` appears after byte 0, byte 0 is the reloc count and the scan starts at byte 1. Otherwise drop byte 0 and emit the rest as raw bytes.

## What xld writes

Relative sections are placed in S4 order. The cursor starts at `0x100`. BSCT (id 3) of at most 256 bytes is placed at 0 and does not move the cursor. Absolute chunks are placed at the S2 offset; the base is not added. `cstart` lands DSCT at `0x0100` (size `0x100`), PSCT at `0x0200` (size `0x0E`), and ASCT at `0xFFFE` (size 2).

The entry point is the word at `FFFE` when that word points into a PSCT contribution, otherwise the first PSCT global. Unsatisfied externals are written as 0. The map for `cstart` reports:

```
 ASCT = 0002
 BSCT = 0000
 DSCT = 0100
 PSCT = 000E
```

Entry `0200`. Unsatisfied `.clrio` at `0205` and `main` at `0209`.

The absolute image is S1 records (at most 32 data bytes) for contiguous written spans, then a linked SD only if the object had an SD, then S9 at address 0. The linked SD is not the object SD. The `cstart` image ends with `SD0502000008F0`: count `05`, 16-bit address `0200` (the entry), data `00 08`, checksum `F0`. The loader writes S1, S2, and S3 only. SD, S7, S8, S9, and S0 are not stored as code.

The image `cstart_real.mik` is:

```
S111020010CE0200BD00004FBD00007E0200C3
S105FFFE0200FB
SD0502000008F0
S9030000FC
```
