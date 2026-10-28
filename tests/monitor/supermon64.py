#!/usr/bin/env python3
"""Scripted SuperMon 64 session for wave WD.

Covers r m a d f g j h t c l s x and the four radices.
The instruction suite still owns single-step register compares.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    args = ap.parse_args()
    binary = args.binary
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
        print(f"binary is not executable: {binary}")
        return 1

    tmp = tempfile.mkdtemp(prefix="mimic-wd-")
    s19 = os.path.join(tmp, "save.s19")
    quoted = os.path.join(tmp, "quote.s19")

    lines = [
        "?",
        "$2000",
        ">1000 11 22 33 44 55 66 77 88:",
        "m +4096 1007",
        ">2000 AA BB CC DD EE FF 01 02",
        "m &20000 2007",
        ">1800 10 20 30 40 50 60 70 80",
        "m %1100000000000 1807",
        "r",
        "; 4321 00 AA BB 1111 2222 3333 4444",
        "r",
        "; PC=1234 A=55",
        "r",
        "; E=30",
        "r",
        "a 2000 lda #+18",
        "d 2000 2001",
        "d 2000",
        "a 2010 lda ,x",
        "a 2012 lda 5,x",
        "a 2014 lda $30",
        "a 2016 lda $1234",
        "a 2019 ldaa #$FF",
        "a 201B lsla",
        "a 2020 bra 2020",
        "a 2030 lda #%10010",
        "a 2060",
        "nop",
        "",
        "d 2060 2060",
        "f 3500 3503 ff",
        "m 3500 3503",
        "h 3500 3503 ff ff ff ff",
        "t 3500 3503 3600",
        "m 3600 3603",
        ":3601 00",
        "c 3500 3503 3600",
        ">3700 48 49",
        "h 3700 3701 'HI",
        ">3800 11 22 33 44 55 66 77 88",
        "t 3800 3802 3801",
        "m 3800 3807",
        ">4100 DE AD BE EF",
        f"s {s19} 4100 4103",
        ":4100 00 00 00 00",
        f"l {s19}",
        "m 4100 4103",
        f's "{quoted}",1,4100,4104',
        ":4100 00 00 00 00",
        f"l {quoted}",
        "m 4100 4103",
        "b 2500",
        "b",
        "bc",
        ":2100 13",
        "g 2100",
        "; S=2200",
        ":2210 39",
        "j 2210",
        ":2300 12 12 12 12 12 12 12 12 12 12 12",
        "; PC=2300",
        "S 10",
        "R",
        "S",
        "R",
        "@",
        "h",
        "x",
    ]
    script = "\n".join(lines) + "\n"
    try:
        proc = subprocess.run(
            [binary, "-i"],
            input=script,
            text=True,
            capture_output=True,
            timeout=5,
        )
    except subprocess.TimeoutExpired as exc:
        print("FAIL timeout")
        sys.stdout.write(exc.stdout or "")
        sys.stderr.write(exc.stderr or "")
        return 1

    raw = proc.stdout + proc.stderr
    # The prompt has no newline. A command that prints nothing leaves the
    # next prompt on the same line, so "> " can repeat. ">1000" is a dump
    # line and is left alone because the character after ">" is not a space.
    def strip_prompt(line):
        while line.startswith("> ") or line.startswith("m9> "):
            line = line[4:] if line.startswith("m9> ") else line[2:]
        return line

    out = "\n".join(strip_prompt(line) for line in raw.splitlines())
    fails = []

    def need(ok, msg):
        if not ok:
            fails.append(msg)

    need("Mimic SuperMon" in out, "banner names Mimic SuperMon")
    need("SuperMon 64" not in out and "SuperMon 67" not in out, "old monitor name")
    need("or H" not in out and "H for help" not in out, "H is documented as help")
    need(out.count("Mimic: S [n] step") == 1, "bare h reprinted the help screen")
    need("hunt" in out, "help names hunt")
    need("$2000" in out and "+8192" in out and "&20000" in out, "radix forms")
    need("%10000000000000" in out, "binary form of $2000")
    need(">1000 11 22 33 44 55 66 77 88:" in out, "decimal address m +4096")
    need(">2000 AA BB CC DD EE FF 01 02:" in out, "octal address m &20000")
    need(">1800 10 20 30 40 50 60 70 80:" in out, "binary address")
    need("PC=" not in out, "old PC= register dump")
    need(re.search(r"(?m)^ PC   CC EFHINZVC ", out) is not None, "register name line")
    need(";0000 00 ........ 00 00 -- -- 00 0000 0000 0000 0000 ---- --" in out,
         "unread 6309 registers are dashes")
    need(";4321 00 ........ AA BB -- -- 00 1111 2222 3333 4444 ---- --" in out,
         "positional register overtype")
    need(";1234 00 ........ 55 BB -- -- 00 1111 2222 3333 4444 ---- --" in out,
         "named register edit")
    need(";1234 00 ........ 55 BB 30 -- 00 1111 2222 3333 4444 ---- --" in out,
         "writing E reveals that register only")
    need("86 12" in out and "LDA" in out, "a 2000 lda #+18")
    need("A6 84" in out, "lda ,x")
    need("A6 05" in out, "lda 5,x")
    need("96 30" in out, "lda $30 direct")
    need("B6 12 34" in out, "lda $1234 extended")
    need("86 FF" in out, "ldaa #$FF alias")
    need("ASLA" in out, "lsla alias")
    need("20 FE" in out, "bra 2020")
    need("NOP" in out, "assemble-until-blank nop")

    head, _, _ = out.partition("a 2010")
    before_next = head.split(". 2002", 1)[0]
    need(before_next.count(". 2000") == 2, "d start end stopped after one instruction")
    need(". 2012" in head and ". 2014" not in head, "d 2000 stays inside 20 bytes")
    need(re.search(r"\. 2060 12\s+NOP", out) is not None, "d 2060 shows NOP")
    mnem_cols = []
    for line in out.splitlines():
        if not line.startswith(". ") or len(line) < 7:
            continue
        i = 6
        while (i + 2 < len(line) and line[i] == " "
               and all(c in "0123456789ABCDEF" for c in line[i + 1:i + 3])):
            i += 3
        while i < len(line) and line[i] == " ":
            i += 1
        if i < len(line):
            mnem_cols.append(i)
    need(len(mnem_cols) >= 3 and len(set(mnem_cols)) == 1,
         "disassembly mnemonics share one column")
    need(". 2000 86 12             LDA   #$12" in out, "two-byte mnemonic column")
    need(". 2060 12                NOP" in out, "one-byte mnemonic column")

    need(">3500 FF FF FF FF" in out, "fill")
    need(re.search(r"(?m)^3500$", out) is not None, "hunt found 3500")
    need(">3600 FF FF FF FF" in out, "transfer")
    need(re.search(r"(?m)^3501$", out) is not None, "compare printed 3501")
    need(re.search(r"(?m)^3700$", out) is not None, "hunt text HI")
    need(">3800 11 11 22 33" in out, "overlapping transfer")
    need(f"Saved 4100..4103 to {s19}" in out, "posix save")
    need(f"Loaded" in out and s19 in out, "posix load")
    need(out.count(">4100 DE AD BE EF ") >= 2, "loaded image matches the save")
    need(f"Saved 4100..4103 to {quoted}" in out, "quoted save end is exclusive")
    need("Breakpoint set at 2500" in out, "breakpoint set")
    need("Breakpoints:" in out and re.search(r"(?m)^(?:m9> |> )?  2500$", raw) is not None, "b lists 2500")
    need("All breakpoints cleared." in out, "bc clears")
    need("$000B" not in out and "$00BC" not in out, "b and bc were read as hex")
    need("Halted" in out, "g of SYNC halted")
    need("Returned from 2210" in out, "j returned from RTS")
    need(";230A " in out, "S 10 stepped ten decimal instructions")
    need(";230B " in out, "bare S stepped once")
    need("@ is not a host disk. Use l and s for files." in out, "@ reject")
    need("h <start> <end>" in out, "bare h prints hunt usage")
    need("Exiting mimic09." in out, "x exits")
    need(proc.returncode == 0, f"exit {proc.returncode}")

    if fails:
        print(f"FAIL {len(fails)}")
        for msg in fails:
            print(f"  {msg}")
        sys.stdout.write(out)
        return 1
    print("PASS Mimic SuperMon")
    return 0


if __name__ == "__main__":
    sys.exit(main())
