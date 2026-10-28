#!/usr/bin/env python3
"""Round-trip Hitachi 6309 encodings through Mimic SuperMon.

Assembles each line, disassembles it, then assembles that text again.
The CPU still executes only the 6309 subset already in cpu.c. This test
checks the assembler and the disassembler.
"""

import argparse
import os
import subprocess
import sys

# (source, expected bytes). Addresses are 16 apart so a 5-byte
# instruction cannot run into the next one.
CASES = [
    ("SEXW", [0x14]),
    ("LDQ #$12345678", [0xCD, 0x12, 0x34, 0x56, 0x78]),
    ("LDQ <$30", [0x10, 0xDC, 0x30]),
    ("STQ $1234", [0x10, 0xFD, 0x12, 0x34]),
    ("LDE #$AB", [0x11, 0x86, 0xAB]),
    ("ADDE <$10", [0x11, 0x9B, 0x10]),
    ("ADDF ,X", [0x11, 0xEB, 0x84]),
    ("STE $2000", [0x11, 0xB7, 0x20, 0x00]),
    ("AIM #$0F,<$30", [0x02, 0x0F, 0x30]),
    ("OIM #$80,$1234", [0x71, 0x80, 0x12, 0x34]),
    ("EIM #$0F,1,X", [0x65, 0x0F, 0x01]),
    ("OIM #$01,E,X", [0x61, 0x01, 0x87]),
    ("TIM #$FF,E,X", [0x6B, 0xFF, 0x87]),
    ("ADDR X,Y", [0x10, 0x30, 0x12]),
    ("CMPR E,F", [0x10, 0x37, 0xEF]),
    ("PSHSW", [0x10, 0x38]),
    ("PULUW", [0x10, 0x3B]),
    ("COME", [0x11, 0x43]),
    ("LSLD", [0x10, 0x48]),
    ("ASLD", [0x10, 0x48]),
    ("LDW #$1234", [0x10, 0x86, 0x12, 0x34]),
    ("TFM X+,Y+", [0x11, 0x38, 0x12]),
    ("TFM U-,S-", [0x11, 0x39, 0x34]),
    ("TFM X+,Y", [0x11, 0x3A, 0x12]),
    ("TFM D,X+", [0x11, 0x3B, 0x01]),
    ("BAND A,5,1,<$40", [0x11, 0x30, 0x69, 0x40]),
    ("LDMD #$01", [0x11, 0x3D, 0x01]),
    ("BITMD #$80", [0x11, 0x3C, 0x80]),
    ("MULD #$100", [0x11, 0x8F, 0x01, 0x00]),
    ("DIVD #$10", [0x11, 0x8D, 0x10]),
    ("LDE E,X", [0x11, 0xA6, 0x87]),
    ("NEGD", [0x10, 0x40]),
    ("CLRW", [0x10, 0x5F]),
    ("TFR W,PC", [0x1F, 0x65]),
    ("TFR A,B", [0x1F, 0x89]),
    ("LDA #$2A", [0x86, 0x2A]),
    ("LDAA #$FF", [0x86, 0xFF]),
    ("LDA A,X", [0xA6, 0x86]),
    ("STW <$40", [0x10, 0x97, 0x40]),
    ("BOR CC,0,7,$00", [0x11, 0x32, 0x07, 0x00]),
    ("DIVQ <$20", [0x11, 0x9E, 0x20]),
    ("SWI3", [0x11, 0x3F]),
    ("LDBT B,3,0,<$12", [0x11, 0x36, 0x98, 0x12]),
    ("STBT A,1,2,$05", [0x11, 0x37, 0x4A, 0x05]),
    ("ORD ,Y", [0x10, 0xAA, 0xA4]),
    ("LDQ ,X", [0x10, 0xEC, 0x84]),
    ("EXG D,W", [0x1E, 0x06]),
    ("ADCD #$0001", [0x10, 0x89, 0x00, 0x01]),
]

REJECTS = [
    "lde #$100",
    "tfm x+,y-",
    "tfm w+,x+",
    "band d,1,2,$10",
    "oim #$01",
]


def run(binary, script):
    proc = subprocess.run(
        [binary, "-i"],
        input=script,
        text=True,
        capture_output=True,
        timeout=10,
    )
    return proc.returncode, proc.stdout + proc.stderr


def parse_listing(line):
    """`a ADDR` / `. ADDR` plus the five-column opcode field."""
    if len(line) < 6 or line[1] != " " or line[0] not in "a.":
        return None
    try:
        addr = int(line[2:6], 16)
    except ValueError:
        return None
    i = 6
    raw = []
    hexdigits = "0123456789ABCDEF"
    while (i + 2 < len(line) and line[i] == " "
           and line[i + 1] in hexdigits and line[i + 2] in hexdigits):
        raw.append(int(line[i + 1:i + 3], 16))
        i += 3
    if not raw:
        return None
    while i < len(line) and line[i] == " ":
        i += 1
    return addr, raw, line[i:]


def listings(out, kind):
    rows = []
    for line in out.splitlines():
        if not line.startswith(kind + " "):
            continue
        row = parse_listing(line)
        if row is not None:
            rows.append(row)
    return rows


def mnem_columns(out):
    cols = []
    for line in out.splitlines():
        if not line.startswith(". ") or len(line) < 7:
            continue
        i = 6
        hexdigits = "0123456789ABCDEF"
        while (i + 2 < len(line) and line[i] == " "
               and line[i + 1] in hexdigits and line[i + 2] in hexdigits):
            i += 3
        while i < len(line) and line[i] == " ":
            i += 1
        if i < len(line):
            cols.append(i)
    return cols


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    args = ap.parse_args()
    binary = args.binary
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
        print(f"binary is not executable: {binary}")
        return 1

    fails = []

    def need(ok, msg):
        if not ok:
            fails.append(msg)

    lines = ["?"]
    for i, (text, _raw) in enumerate(CASES):
        lines.append(f"a {0x2000 + i * 16:04X} {text}")
    for i in range(len(CASES)):
        addr = 0x2000 + i * 16
        lines.append(f"d {addr:04X} {addr:04X}")
    for i in range(len(REJECTS)):
        addr = 0x3F00 + i * 16
        lines.append(f"> {addr:04X} EE EE EE EE EE EE EE EE")
    for i, text in enumerate(REJECTS):
        lines.append(f"a {0x3F00 + i * 16:04X} {text}")
    for i in range(len(REJECTS)):
        addr = 0x3F00 + i * 16
        lines.append(f"m {addr:04X} {addr + 7:04X}")
    lines.append("x")

    try:
        rc, out = run(binary, "\n".join(lines) + "\n")
    except subprocess.TimeoutExpired as exc:
        print("FAIL timeout")
        sys.stdout.write(exc.stdout or "")
        sys.stderr.write(exc.stderr or "")
        return 1

    assembled = listings(out, "a")
    shown = listings(out, ".")
    need(rc == 0, f"exit {rc}")
    need(out.count("Mimic: S [n] step") == 1, "help step line")
    need("assemble one 6809 or 6309 instruction" in out, "help names 6309")
    need(len(assembled) == len(CASES),
         f"assembled {len(assembled)} of {len(CASES)}")
    need(len(shown) == len(CASES), f"disassembled {len(shown)} of {len(CASES)}")

    texts = []
    n = min(len(assembled), len(shown), len(CASES))
    for i in range(n):
        addr, raw, text = assembled[i]
        daddr, draw, dtext = shown[i]
        expect = CASES[i][1]
        where = f"{CASES[i][0]} at {0x2000 + i * 16:04X}"
        need(addr == 0x2000 + i * 16 and raw == expect,
             f"assemble {where}: got {addr:04X} {raw} want {expect}")
        need(daddr == addr and draw == expect and dtext == text,
             f"disasm {where}: {daddr:04X} {draw} {dtext!r} vs {text!r}")
        texts.append(text)

    cols = mnem_columns(out)
    need(len(cols) == len(CASES) and len(set(cols)) == 1,
         "disassembly mnemonics share one column")
    need("a 2000 14                SEXW" in out, "SEXW column")
    need("a 2010 CD 12 34 56 78    LDQ   #$12345678" in out, "LDQ column")
    need(out.splitlines().count("?") == len(REJECTS),
         f"rejected lines: {out.splitlines().count('?')}")
    for i in range(len(REJECTS)):
        addr = 0x3F00 + i * 16
        need(f">{addr:04X} EE EE EE EE EE EE EE EE:" in out,
             f"reject left {addr:04X} unchanged ({REJECTS[i]})")

    again = []
    for i, text in enumerate(texts):
        again.append(f"a {0x4000 + i * 16:04X} {text}")
    again.append("x")
    if texts:
        try:
            rc2, out2 = run(binary, "\n".join(again) + "\n")
        except subprocess.TimeoutExpired as exc:
            print("FAIL timeout on reassemble")
            sys.stdout.write(exc.stdout or "")
            return 1
        reassembled = listings(out2, "a")
        need(rc2 == 0, f"reassemble exit {rc2}")
        need(len(reassembled) == len(texts),
             f"reassembled {len(reassembled)} of {len(texts)}")
        for i in range(min(len(reassembled), len(texts), len(CASES))):
            addr, raw, _text = reassembled[i]
            expect = CASES[i][1]
            need(addr == 0x4000 + i * 16 and raw == expect,
                 f"reassemble {CASES[i][0]}: {addr:04X} {raw} from {texts[i]!r}")

    if fails:
        print(f"FAIL {len(fails)}")
        for msg in fails:
            print(f"  {msg}")
        sys.stdout.write(out)
        return 1
    print("PASS 6309 assembler")
    return 0


if __name__ == "__main__":
    sys.exit(main())
