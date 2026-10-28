#!/usr/bin/env python3
"""Payload checks for the legacy assembler, linker directive, and hex filter."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "bin"
TMP = Path("/tmp/mimic-legacy")
TMP.mkdir(parents=True, exist_ok=True)


def run(args, stdin=None):
    proc = subprocess.run(
        args, cwd=ROOT, input=stdin, capture_output=True
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("latin1", "replace"))
        sys.stderr.write(proc.stdout.decode("latin1", "replace"))
        raise SystemExit("command failed (%s): %s" % (proc.returncode, " ".join(args)))
    return proc


def hexval(c):
    if "0" <= c <= "9":
        return ord(c) - 48
    if "A" <= c <= "F":
        return ord(c) - 55
    if "a" <= c <= "f":
        return ord(c) - 87
    return -1


def load_srec(text):
    mem = {}
    for raw in text.splitlines():
        line = raw.strip()
        if len(line) < 4 or line[0] not in "Ss":
            continue
        typ = hexval(line[1])
        if typ not in (1, 2, 3):
            continue
        length = (hexval(line[2]) << 4) | hexval(line[3])
        alen = 2 if typ == 1 else 3
        dlen = length - alen - 1
        body = line[4:4 + (alen + dlen) * 2]
        addr = 0
        for i in range(alen):
            addr = (addr << 8) | ((hexval(body[i * 2]) << 4) | hexval(body[i * 2 + 1]))
        data = body[alen * 2:]
        a = addr & 0xFFFF
        for i in range(dlen):
            mem[a] = (hexval(data[i * 2]) << 4) | hexval(data[i * 2 + 1])
            a = (a + 1) & 0xFFFF
    return mem


def norm_lines(path):
    text = Path(path).read_bytes().replace(b"\r", b"").decode("latin1")
    return [ln for ln in text.splitlines() if ln.strip()]


def load_intel(text):
    mem = {}
    saw_eof = False
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise SystemExit("non-Intel record: %s" % line)
        nibs = []
        hexpart = line[1:]
        if len(hexpart) % 2:
            raise SystemExit("odd Intel record: %s" % line)
        for i in range(0, len(hexpart), 2):
            nibs.append((hexval(hexpart[i]) << 4) | hexval(hexpart[i + 1]))
        count, addr, typ = nibs[0], (nibs[1] << 8) | nibs[2], nibs[3]
        if sum(nibs[: 4 + count + 1]) & 0xFF:
            raise SystemExit("bad Intel checksum: %s" % line)
        if typ == 1:
            saw_eof = True
            if count != 0 or addr != 0:
                raise SystemExit("unexpected EOF record: %s" % line)
            continue
        if typ != 0:
            continue
        for i in range(count):
            mem[addr + i] = nibs[4 + i]
    if not saw_eof:
        raise SystemExit("Intel image has no EOF record")
    return mem


def assemble_org(name, lines):
    src = TMP / name
    src.write_text(
        "\tORG\t$2000\n" + "".join("\t%s\n" % ln for ln in lines) + "\tEND\n",
        encoding="latin1",
    )
    rel = TMP / (name + ".rel")
    run([str(BIN / "a9"), str(src), "-nh", "-o", str(rel)])
    return load_srec(rel.read_text(encoding="latin1"))


def walk_image(mem, cases, origin=0x2000):
    addr = origin
    for text, hx in cases:
        raw = bytes.fromhex(hx)
        got = bytearray()
        for i in range(len(raw)):
            b = mem.get(addr + i)
            if b is None:
                raise SystemExit("%s missing byte at %04X" % (text, addr + i))
            got.append(b)
        if bytes(got) != raw:
            raise SystemExit(
                "%s at %04X: %s != %s" % (text, addr, bytes(got).hex(), raw.hex())
            )
        addr += len(raw)
    extra = [a for a in mem if a < origin or a >= addr]
    if extra:
        raise SystemExit(
            "unexpected bytes at %s" % ", ".join("%04X=%02X" % (a, mem[a]) for a in sorted(extra)[:8])
        )


def check_6309():
    """Hitachi 6309 bytes, with 6809 encodings left unchanged.

    The first CLRD is before OPT 6309. Pass 2 clears the flag and reads
    the file again, so that CLRD stays CLRB, CLRA (5F 4F).
    """
    guard = [
        ("CLRD", "5F4F"),
        ("LDA #$12", "8612"),
        ("LDAA #$FF", "86FF"),
        ("CMPD #$1234", "10831234"),
        ("LDY #$0001", "108E0001"),
        ("CMPU #$0002", "11830002"),
        ("CMPS #$0003", "118C0003"),
        ("SWI2", "103F"),
        ("SWI3", "113F"),
        ("TFR A,B", "1F89"),
        ("TFR D,X", "1F01"),
        ("PSHS A", "3402"),
        ("PSHS A,B,X", "3416"),
        ("LDA ,X", "A684"),
        ("LDA A,X", "A686"),
        ("LDA -1,X", "A61F"),
    ]
    walk_image(assemble_org("guard6809.asm", [t for t, _h in guard]), guard)

    prog = [
        ("CLRD", "5F4F"),
        "OPT 6309",
        ("SEXW", "14"),
        ("LDQ #$12345678", "CD12345678"),
        ("LDQ #305419896", "CD12345678"),
        ("LDQ #1+1", "CD00000002"),
        ("LDQ <$30", "10DC30"),
        ("LDQ ,X", "10EC84"),
        ("STQ $1234", "10FD1234"),
        ("LDE #$AB", "1186AB"),
        ("ADDE <$10", "119B10"),
        ("ADDF ,X", "11EB84"),
        ("STE $2000", "11B72000"),
        ("OIM #$01,<$10", "010110"),
        ("AIM #$0F,<$30", "020F30"),
        ("OIM #$80,$1234", "71801234"),
        ("EIM #$0F,1,X", "650F01"),
        ("OIM #$01,E,X", "610187"),
        ("TIM #$FF,E,X", "6BFF87"),
        ("ADDR X,Y", "103012"),
        ("ADCR X,Y", "103112"),
        ("SUBR X,Y", "103212"),
        ("SBCR X,Y", "103312"),
        ("ANDR X,Y", "103412"),
        ("ORR X,Y", "103512"),
        ("EORR X,Y", "103612"),
        ("CMPR E,F", "1037EF"),
        ("PSHSW", "1038"),
        ("PULSW", "1039"),
        ("PSHUW", "103A"),
        ("PULUW", "103B"),
        ("NEGD", "1040"),
        ("COMD", "1043"),
        ("LSRD", "1044"),
        ("RORD", "1046"),
        ("ASRD", "1047"),
        ("LSLD", "1048"),
        ("ASLD", "1048"),
        ("ROLD", "1049"),
        ("DECD", "104A"),
        ("INCD", "104C"),
        ("TSTD", "104D"),
        ("COMW", "1053"),
        ("LSRW", "1054"),
        ("RORW", "1056"),
        ("ROLW", "1059"),
        ("DECW", "105A"),
        ("INCW", "105C"),
        ("TSTW", "105D"),
        ("CLRW", "105F"),
        ("SUBW #$1234", "10801234"),
        ("SUBW <$10", "109010"),
        ("CMPW #$1234", "10811234"),
        ("SBCD #$1234", "10821234"),
        ("ANDD #$1234", "10841234"),
        ("BITD #$1234", "10851234"),
        ("LDW #$1234", "10861234"),
        ("EORD #$1234", "10881234"),
        ("ADCD #$0001", "10890001"),
        ("ORD #$1234", "108A1234"),
        ("ORD ,Y", "10AAA4"),
        ("ADDW #$1234", "108B1234"),
        ("ADDW ,X", "10AB84"),
        ("STW <$40", "109740"),
        ("BAND A,5,1,<$40", "11306940"),
        ("BIAND A,5,1,<$40", "11316940"),
        ("BOR CC,0,7,$00", "11320700"),
        ("BOR C,0,7,$00", "11320700"),
        ("BIOR A,5,1,<$40", "11336940"),
        ("BEOR A,5,1,<$40", "11346940"),
        ("BIEOR A,5,1,<$40", "11356940"),
        ("LDBT B,3,0,<$12", "11369812"),
        ("STBT A,1,2,$05", "11374A05"),
        ("TFM X+,Y+", "113812"),
        ("TFM U-,S-", "113934"),
        ("TFM X+,Y", "113A12"),
        ("TFM D,X+", "113B01"),
        ("BITMD #$80", "113C80"),
        ("LDMD #$01", "113D01"),
        ("COME", "1143"),
        ("DECE", "114A"),
        ("INCE", "114C"),
        ("TSTE", "114D"),
        ("CLRE", "114F"),
        ("COMF", "1153"),
        ("DECF", "115A"),
        ("INCF", "115C"),
        ("TSTF", "115D"),
        ("CLRF", "115F"),
        ("SUBE #$01", "118001"),
        ("CMPE #$01", "118101"),
        ("DIVD #$10", "118D10"),
        ("DIVQ <$20", "119E20"),
        ("MULD #$100", "118F0100"),
        ("SUBF #$01", "11C001"),
        ("CMPF #$01", "11C101"),
        ("LDF #$01", "11C601"),
        ("STF $2000", "11F72000"),
        ("LDE E,X", "11A687"),
        ("LDA E,X", "A687"),
        ("TFR W,PC", "1F65"),
        ("TFR A,B", "1F89"),
        ("TFR E,F", "1FEF"),
        ("TFR 0,V", "1FC7"),
        ("EXG D,W", "1E06"),
        ("LDA #$2A", "862A"),
        ("LDAA #$FF", "86FF"),
        ("LDA A,X", "A686"),
        ("SWI3", "113F"),
        ("CLRD", "104F"),
    ]
    lines = []
    cases = []
    for item in prog:
        if isinstance(item, str):
            lines.append(item)
        else:
            lines.append(item[0])
            cases.append(item)
    walk_image(assemble_org("ops6309.asm", lines), cases)

    bad_src = TMP / "ops6309_bad.asm"
    bad_rel = TMP / "ops6309_bad.rel"
    bad_src.write_text(
        "\tORG\t$2000\n"
        "\tTFM X+,Y-\n"
        "\tTFM W+,X+\n"
        "\tNEGW\n"
        "\tASRW\n"
        "\tLSLW\n"
        "\tBAND A,5,1,$1234\n"
        "\tBAND A,5,1,E,X\n"
        "\tEND\n",
        encoding="latin1",
    )
    bad = subprocess.run(
        [str(BIN / "a9"), str(bad_src), "-nh", "-o", str(bad_rel)],
        cwd=ROOT, capture_output=True,
    )
    if bad.returncode != 1:
        raise SystemExit("illegal 6309 forms exited %s" % bad.returncode)
    if b"Errors detected" not in bad.stderr:
        raise SystemExit("illegal 6309 forms did not report errors")
    bad_mem = load_srec(bad_rel.read_text(encoding="latin1"))
    if bad_mem:
        raise SystemExit("illegal 6309 forms emitted %s" % bad_mem)

    lst = TMP / "clrio.lst"
    run([
        str(BIN / "a9"), "lib/src/clrio.asm", "-nh",
        "-l", str(lst), "-o", str(TMP / "clrio.rel"),
    ])
    saw = False
    for line in lst.read_text(encoding="latin1").splitlines():
        if "CLRD" not in line:
            continue
        saw = True
        if "5F 4F" not in line or "10 4F" in line:
            raise SystemExit("clrio CLRD listing is %s" % line)
    if not saw:
        raise SystemExit("clrio listing has no CLRD line")


def main():
    run([str(BIN / "a9"), "tests/smoke/ops.asm", "-nh", "-o", str(TMP / "ops.rel")])
    run([str(BIN / "xld"), "-nh", str(TMP / "ops.rel"), "-o", str(TMP / "ops.mik")])
    ops = load_srec((TMP / "ops.mik").read_text(encoding="latin1"))
    expect = bytes.fromhex("128C00101F016F801A010516000012")
    got = bytes(ops.get(0x100 + i, 0) for i in range(len(expect)))
    if got != expect:
        raise SystemExit("ops image %s != %s" % (got.hex(), expect.hex()))
    blob = bytes(ops[a] for a in sorted(ops))
    if b"\x86\x01" in blob:
        raise SystemExit("IFNE 0 assembled LDA #$01")

    (TMP / "nostamp.dir").write_text("FILE tests/golden/cstart_real.rel\nEND\n")
    run([
        str(BIN / "xld"), "-nh", "-d", str(TMP / "nostamp.dir"),
        "-o", str(TMP / "nostamp.mik"), "-map", str(TMP / "nostamp.map"),
    ])
    if norm_lines(TMP / "nostamp.mik") != norm_lines(ROOT / "tests/golden/cstart_real.mik"):
        raise SystemExit("DIR without SECTION changed the cstart image")
    mp = (TMP / "nostamp.map").read_text(encoding="latin1")
    for line in (
        "ASCT = 0002", "BSCT = 0000", "DSCT = 0100", "PSCT = 000E",
        "Entry Point at: 0200", ".clrio", "main",
    ):
        if line not in mp:
            raise SystemExit("cstart map missing %s" % line)

    (TMP / "pse.dir").write_text(
        "FILE tests/golden/cstart_real.rel\nSECTION PSCT @ E000\nEND\n"
    )
    run([
        str(BIN / "xld"), "-nh", "-d", str(TMP / "pse.dir"),
        "-o", str(TMP / "pse.mik"), "-map", str(TMP / "pse.map"),
    ])
    pse = load_srec((TMP / "pse.mik").read_text(encoding="latin1"))
    raw = [pse.get(0xE000 + i) for i in range(14)]
    if any(b is None for b in raw):
        raise SystemExit("PSCT @ E000 did not place 14 bytes at E000")
    window = bytes(raw)
    if window[:4] != bytes.fromhex("10CE0200") or window[11:14] != bytes.fromhex("7EE000"):
        raise SystemExit("moved cstart image is %s" % window.hex())
    if pse.get(0xFFFE) != 0xE0 or pse.get(0xFFFF) != 0x00:
        raise SystemExit("reset vector did not follow PSCT to E000")
    if 0x0200 in pse:
        raise SystemExit("PSCT @ E000 still wrote the default 0200 image")
    if "Entry Point at: E000" not in (TMP / "pse.map").read_text(encoding="latin1"):
        raise SystemExit("entry point stayed off E000")

    mik = (ROOT / "tests/golden/cstart_real.mik").read_bytes()
    proc = run([str(BIN / "hex"), "0200", "020f"], stdin=mik)
    if proc.stdout[:1] != b":":
        raise SystemExit("hex wrote a banner on stdout")
    if b"PROM Image Filter" not in proc.stderr:
        raise SystemExit("hex banner missing from stderr")
    intel = load_intel(proc.stdout.decode("latin1"))
    golden = load_srec(mik.decode("latin1"))
    # The window is a PROM image: loaded bytes stay, unloaded bytes are FF.
    want = {a: golden[a] if a in golden else 0xFF for a in range(0x200, 0x210)}
    if intel != want:
        raise SystemExit("hex window %s != %s" % (intel, want))

    bare = run([str(BIN / "hex"), "e000", "e000"], stdin=b"S104E000A576\n")
    bare_mem = load_intel(bare.stdout.decode("latin1"))
    if bare_mem.get(0xE000) != 0xA5:
        raise SystemExit("hex did not accept a base-16 address: %s" % bare.stdout)

    inv = run([str(BIN / "hex"), "-D", "e000", "e000"], stdin=b"S104E000A576\n")
    if load_intel(inv.stdout.decode("latin1")).get(0xE000) != 0x5A:
        raise SystemExit("-D did not invert A5")

    srec = run([str(BIN / "hex"), "-W", "-A", "-S", "e000", "e000"], stdin=b"S104E000A576\n")
    text = srec.stdout.decode("latin1").replace("\r", "")
    if not text.startswith("S1") or "S9030000FC" not in text:
        raise SystemExit("-S did not emit S1 records: %s" % text)
    if load_srec(text).get(0xE000) != 0xA5:
        raise SystemExit("-W/-A changed the data byte")

    gap_in = b"S1040200AA4F\nS10402105594\nS9030000FC\n"
    gap = run([str(BIN / "hex"), "0200", "0210"], stdin=gap_in)
    gap_text = gap.stdout.decode("latin1").replace("\r", "")
    records = [ln for ln in gap_text.splitlines() if ln.startswith(":")]
    if len(records) != 3:
        raise SystemExit("gap should be two data records plus EOF, got %s" % records)
    want_gap = {0x200: 0xAA, 0x210: 0x55}
    for a in range(0x201, 0x210):
        want_gap[a] = 0xFF
    if load_intel(gap_text) != want_gap:
        raise SystemExit("gap records: %s" % records)

    usage = subprocess.run([str(BIN / "hex")], cwd=ROOT, capture_output=True)
    if usage.returncode == 0 or usage.stdout:
        raise SystemExit("hex usage leaked onto stdout or exited 0")
    if b"Usage:" not in usage.stderr:
        raise SystemExit("hex usage missing from stderr")

    check_6309()
    print("legacy-smoke passed")


if __name__ == "__main__":
    main()
