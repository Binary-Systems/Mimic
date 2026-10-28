#!/usr/bin/env python3
"""
Mimic09 Instruction Set Validation Suite
Covers 6809 and 6309 instructions + addressing modes.
Each test "enters by hand" using : poke in the monitor, S single step, R for state.
Compares to documented expected from Motorola/Hitachi refs.
Run with ../tests/run-instr-suite.sh (from mimic09 dir) or the python.
"""
import argparse
import subprocess
import re
import sys
import os

# 6309-only opcodes that are not 6809. An unprefixed 6809 opcode used as a
# stand-in for ADDE/LDE/ADDF is a mis-encode and must fail.
_OK6309 = {0x01, 0x02, 0x05, 0x0B, 0x14, 0x15, 0xCD, 0x10, 0x11}

def is_misencoded_6309(code):
    if not code:
        return False
    if code[0] in _OK6309:
        return False
    # These four bytes are real 6809 ops (ADDA, LDA, LDA, ADDB).
    return code[0] in (0x8B, 0x96, 0x86, 0xDB)

def misencode_self_check():
    bad = [
        [0x8B, 0x01],
        [0x96, 0x30],
        [0x86, 0xAB],
        [0xDB, 0x30],
    ]
    good = [
        [0x11, 0x8B, 0x01],
        [0x11, 0x96, 0x30],
        [0x11, 0x86, 0xAB],
        [0x11, 0xDB, 0x30],
        [0x14],
        [0x02, 0x0F, 0x30],
        [0xCD, 0x12, 0x34, 0x56, 0x78],
        [0x11, 0x38],
        [0x11, 0x3B],
        [0x01],
    ]
    for c in bad:
        if not is_misencoded_6309(c):
            raise SystemExit("misencode self-check failed to reject %s" % c)
    for c in good:
        if is_misencoded_6309(c):
            raise SystemExit("misencode self-check rejected a real 6309 op %s" % c)

def find_binary(explicit=None):
    if explicit:
        return os.path.abspath(explicit)
    candidates = [
        "mimic09/bin/mimic09",
        os.path.join(os.path.dirname(__file__), "..", "..", "mimic09", "bin", "mimic09"),
        os.path.join(os.getcwd(), "bin", "mimic09"),
    ]
    for c in candidates:
        if os.path.exists(c) and os.access(c, os.X_OK):
            return os.path.abspath(c)
    return "/Volumes/Drobo0/BSI/Mimic/mimic09/bin/mimic09"

# Last ';' register line. Fields follow the monitor's name line:
# PC CC flags A B E F DP X Y U S V MD.
# Dashes are unread 6309 registers and mean zero. D is A:B.
_REG_LINE = re.compile(
    r";([0-9A-Fa-f]{4}) ([0-9A-Fa-f]{2}) [EFHINZVC.]{8} "
    r"([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) "
    r"([0-9A-Fa-f-]{2}) ([0-9A-Fa-f-]{2}) "
    r"([0-9A-Fa-f]{2}) "
    r"([0-9A-Fa-f]{4}) ([0-9A-Fa-f]{4}) ([0-9A-Fa-f]{4}) ([0-9A-Fa-f]{4}) "
    r"([0-9A-Fa-f-]{4}) ([0-9A-Fa-f-]{2})"
)
_REG_NAMES = (
    "PC", "CC", "A", "B", "E", "F", "DP", "X", "Y", "U", "S", "V", "MD"
)

def _reg_int(text):
    if text and set(text) <= {"-"}:
        return 0
    return int(text, 16)

def parse_r_output(text):
    """Last register value line (post S state)."""
    regs = {}
    matches = list(_REG_LINE.finditer(text))
    if not matches:
        return regs
    groups = matches[-1].groups()
    for name, group in zip(_REG_NAMES, groups):
        regs[name] = _reg_int(group)
    if "A" in regs and "B" in regs:
        regs["D"] = ((regs["A"] & 0xFF) << 8) | (regs["B"] & 0xFF)
    return regs

def build_input(test):
    lines = []
    for addr, val in sorted(test.get("initial_mem", {}).items()):
        lines.append(f":{addr:04X} {val:02X}")
    init = test.get("initial_regs", {})
    # D is applied before A and B so a later byte does not get wiped by D=0000.
    parts = [f"PC={init.get('PC', 0):04X}"]
    if "D" in init:
        parts.append(f"D={init['D']:04X}")
    if "A" in init:
        parts.append(f"A={init['A']:02X}")
    if "B" in init:
        parts.append(f"B={init['B']:02X}")
    for name in ("X", "Y", "U", "S"):
        if name in init:
            parts.append(f"{name}={init[name]:04X}")
    if "CC" in init:
        parts.append(f"CC={init['CC']:02X}")
    if "E" in init or "F" in init:
        parts.append(f"E={init.get('E', 0):02X}")
        parts.append(f"F={init.get('F', 0):02X}")
    lines.append("; " + " ".join(parts))
    code_addr = test["code_addr"]
    hex_str = " ".join(f"{b:02X}" for b in test["code"])
    lines.append(f":{code_addr:04X} {hex_str}")
    lines.append(f"; PC={code_addr:04X}")
    for pre in test.get("prelude", []):
        lines.append(pre)
    lines.append("S")
    lines.append("R")
    mem = test.get("expected_mem") or {}
    if mem:
        lines.append(f"M {min(mem):04X}")
    lines.append("Q")
    return "\n".join(lines) + "\n"

def parse_mem_dump(text):
    found = {}
    # Mimic SuperMon lines are 8 bytes: "> >0200 11 22 33 44 55 66 77 88:".
    for m in re.finditer(
        r"(?m)^(?:m9> |> )?>([0-9A-Fa-f]{4})((?: [0-9A-Fa-f]{2}){8})",
        text,
    ):
        base = int(m.group(1), 16)
        bytes_ = [int(b, 16) for b in m.group(2).split()]
        for i, b in enumerate(bytes_):
            found[(base + i) & 0xFFFF] = b
    # Older 16-byte dumps still parse, so a saved log does not go blank.
    for m in re.finditer(
        r"(?m)^(?:m9> )?([0-9A-Fa-f]{4})  ((?:[0-9A-Fa-f]{2} ){16})",
        text,
    ):
        base = int(m.group(1), 16)
        bytes_ = [int(b, 16) for b in m.group(2).split()]
        for i, b in enumerate(bytes_):
            found[(base + i) & 0xFFFF] = b
    return found

def run_one_test(test, binary):
    name = test["name"]
    cpu_mode = test.get("cpu", "6809")
    cmd = [binary, "-i"]
    if cpu_mode == "6309":
        cmd = [binary, "-c", "6309", "-i"]
    inp = build_input(test)
    try:
        proc = subprocess.run(cmd, input=inp, text=True, capture_output=True, timeout=5)
        output = proc.stdout + proc.stderr
    except Exception as e:
        return {"name": name, "pass": False, "error": str(e)}
    final = parse_r_output(output)
    expected = test.get("expected_regs", {})
    ok = True
    diffs = []
    for k, v in expected.items():
        actual = final.get(k)
        if actual != v:
            ok = False
            diffs.append(f"{k}: expected {v:04X}, got {actual if actual is not None else 'missing'}")
    mem = parse_mem_dump(output)
    for addr, val in (test.get("expected_mem") or {}).items():
        actual = mem.get(addr)
        if actual != val:
            ok = False
            diffs.append(f"mem[{addr:04X}]: expected {val:02X}, got {actual if actual is not None else 'missing'}")
    return {"name": name, "pass": ok, "diffs": diffs, "final": final, "desc": test.get("desc", "") }

TESTS = [
    {"name": "6809_ABX_inh", "cpu": "6809", "desc": "ABX (inh): X = X + B", "code_addr": 0x2000, "code": [0x3A], "initial_regs": {"PC": 0, "X": 0x1234, "B": 0x56, "A": 0, "CC": 0}, "expected_regs": {"PC": 0x2001, "X": 0x128A, "B": 0x56, "CC": 0}},
    {"name": "6809_NOP_inh", "cpu": "6809", "desc": "NOP (inh): PC +=1", "code_addr": 0x2000, "code": [0x12], "initial_regs": {"PC": 0, "X": 0x1111, "CC": 0xFF}, "expected_regs": {"PC": 0x2001, "X": 0x1111, "CC": 0xFF}},
    {"name": "6809_MUL_inh", "cpu": "6809", "desc": "MUL (inh): D=A*B, C=result bit7, Z =0", "code_addr": 0x2000, "code": [0x3D], "initial_regs": {"A": 0x12, "B": 0x34, "CC": 0}, "expected_regs": {"D": 0x03A8, "A": 0x03, "B": 0xA8, "CC": 0x01}},
    {"name": "6809_LDA_imm", "cpu": "6809", "desc": "LDA #$2A (imm): A=2A, V=0 N=0 Z=0, other flags kept", "code_addr": 0x2000, "code": [0x86, 0x2A], "initial_regs": {"A": 0xFF, "CC": 0xFF}, "expected_regs": {"A": 0x2A, "PC": 0x2002, "CC": 0xF1}},
    {"name": "6809_ADDA_imm", "cpu": "6809", "desc": "ADDA #$01 (imm)", "code_addr": 0x2000, "code": [0x8B, 0x01], "initial_regs": {"A": 0x7F, "CC": 0}, "expected_regs": {"A": 0x80, "PC": 0x2002, "CC": 0x2A}},
    {"name": "6809_LDA_dir", "cpu": "6809", "desc": "LDA <$30 (dir)", "code_addr": 0x2000, "code": [0x96, 0x30], "initial_mem": {0x0030: 0xAB}, "initial_regs": {"A": 0, "CC": 0}, "expected_regs": {"A": 0xAB, "PC": 0x2002, "CC": 0x08}},
    {"name": "6809_STA_ext", "cpu": "6809", "desc": "STA $1234 (ext)", "code_addr": 0x2000, "code": [0xB7, 0x12, 0x34], "initial_regs": {"A": 0xCD, "CC": 0}, "initial_mem": {0x1234: 0}, "expected_regs": {"A": 0xCD, "PC": 0x2003, "CC": 0x08}, "expected_mem": {0x1234: 0xCD}},
    {"name": "6809_LDA_idx0", "cpu": "6809", "desc": "LDA ,X (idx0)", "code_addr": 0x2000, "code": [0xA6, 0x00], "initial_regs": {"X": 0x3000, "A": 0}, "initial_mem": {0x3000: 0x42}, "expected_regs": {"A": 0x42, "X": 0x3000, "PC": 0x2002, "CC": 0}},
    {"name": "6809_BRA", "cpu": "6809", "desc": "BRA (rel)", "code_addr": 0x2000, "code": [0x20, 0x03], "initial_regs": {"PC": 0}, "expected_regs": {"PC": 0x2005}},
    {"name": "6809_BEQ_taken", "cpu": "6809", "desc": "BEQ taken (Z=1)", "code_addr": 0x2000, "code": [0x27, 0x02], "initial_regs": {"PC": 0, "CC": 0x04}, "expected_regs": {"PC": 0x2004, "CC": 0x04}},
    {"name": "6809_BEQ_not", "cpu": "6809", "desc": "BEQ not (Z=0)", "code_addr": 0x2000, "code": [0x27, 0x02], "initial_regs": {"PC": 0, "CC": 0}, "expected_regs": {"PC": 0x2002, "CC": 0}},
    {"name": "6309_LDQ_imm", "cpu": "6309", "desc": "LDQ #$12345678 writes D and W; V clears, C stays", "code_addr": 0x2000, "code": [0xCD, 0x12, 0x34, 0x56, 0x78], "initial_regs": {"A": 0, "B": 0, "E": 0, "F": 0, "CC": 0x03}, "expected_regs": {"PC": 0x2005, "A": 0x12, "B": 0x34, "E": 0x56, "F": 0x78, "CC": 0x01}},
    {"name": "6309_LDQ_neg", "cpu": "6309", "desc": "LDQ #$80000000 sets N and clears V", "code_addr": 0x2000, "code": [0xCD, 0x80, 0x00, 0x00, 0x00], "initial_regs": {"CC": 0x02}, "expected_regs": {"PC": 0x2005, "A": 0x80, "B": 0x00, "E": 0x00, "F": 0x00, "CC": 0x08}},
    {"name": "6309_LDQ_zero", "cpu": "6309", "desc": "LDQ #$00000000 sets Z and clears V", "code_addr": 0x2000, "code": [0xCD, 0x00, 0x00, 0x00, 0x00], "initial_regs": {"A": 1, "B": 2, "E": 3, "F": 4, "CC": 0x02}, "expected_regs": {"PC": 0x2005, "A": 0, "B": 0, "E": 0, "F": 0, "CC": 0x04}},
    {"name": "6309_SEXW", "cpu": "6309", "desc": "SEXW sign-extends bit 15 of W into Q", "code_addr": 0x2000, "code": [0x14], "initial_regs": {"E": 0x80, "F": 0x80, "CC": 0}, "expected_regs": {"E": 0x80, "F": 0x80, "PC": 0x2001, "CC": 0x08}},
    {"name": "6809_INC_dir", "cpu": "6809", "desc": "INC <$30 (dir)", "code_addr": 0x2000, "code": [0x0C, 0x30], "initial_mem": {0x0030: 0x7F}, "initial_regs": {"CC": 0}, "expected_regs": {"PC": 0x2002}, "expected_mem": {0x0030: 0x80}},
    {"name": "6809_CLR_ext", "cpu": "6809", "desc": "CLR $1234 (ext) clears N V C, sets Z, leaves E F H I", "code_addr": 0x2000, "code": [0x7F, 0x12, 0x34], "initial_mem": {0x1234: 0xFF}, "initial_regs": {"CC": 0xFF}, "expected_regs": {"PC": 0x2003, "CC": 0xF4}, "expected_mem": {0x1234: 0}},
    {"name": "6809_COMA_inh", "cpu": "6809", "desc": "COMA (inh)", "code_addr": 0x2000, "code": [0x43], "initial_regs": {"A": 0x55, "CC": 0}, "expected_regs": {"A": 0xAA, "PC": 0x2001, "CC": 0x09}},
    {"name": "6809_JSR_dir", "cpu": "6809", "desc": "JSR <$30 jumps to the direct address and pushes the return", "code_addr": 0x2000, "code": [0x9D, 0x30], "initial_regs": {"PC": 0, "S": 0x3000}, "expected_regs": {"PC": 0x0030, "S": 0x2FFE}},
    {"name": "6809_RTS_inh", "cpu": "6809", "desc": "RTS (inh) - after JSR setup", "code_addr": 0x2000, "code": [0x39], "initial_regs": {"PC": 0, "S": 0x2FFE}, "initial_mem": {0x2FFE: 0x20, 0x2FFF: 0x02}, "expected_regs": {"PC": 0x2002, "S": 0x3000}},
    {"name": "6809_PULS", "cpu": "6809", "desc": "PULS #0x01 (CC from stack)", "code_addr": 0x2000, "code": [0x35, 0x01], "initial_regs": {"PC": 0, "S": 0x2FFE, "CC": 0}, "initial_mem": {0x2FFE: 0xFF}, "expected_regs": {"PC": 0x2002, "S": 0x2FFF, "CC": 0xFF}},
    {"name": "6809_LEAX_idx", "cpu": "6809", "desc": "LEAX ,X (idx) - X = X +0", "code_addr": 0x2000, "code": [0x30, 0x00], "initial_regs": {"X": 0x1234}, "expected_regs": {"X": 0x1234, "PC": 0x2002, "CC": 0x00}},
    {"name": "6809_LEAY_ind", "cpu": "6809", "desc": "LEAY [,--U] loads the word, then U is two lower", "code_addr": 0x2000, "code": [0x31, 0xD3], "initial_regs": {"U": 0x3002, "Y": 0, "CC": 0}, "initial_mem": {0x3000: 0x01, 0x3001: 0x14}, "expected_regs": {"Y": 0x0114, "U": 0x3000, "PC": 0x2002, "CC": 0x00}},
    {"name": "6809_TFR", "cpu": "6809", "desc": "TFR X,D", "code_addr": 0x2000, "code": [0x1F, 0x10], "initial_regs": {"X": 0xABCD, "D": 0}, "expected_regs": {"D": 0xABCD, "X": 0xABCD, "PC": 0x2002}},
    {"name": "6809_EXG", "cpu": "6809", "desc": "EXG A,B", "code_addr": 0x2000, "code": [0x1E, 0x89], "initial_regs": {"A": 0x12, "B": 0x34}, "expected_regs": {"A": 0x34, "B": 0x12, "PC": 0x2002}},
    {"name": "6809_SWI", "cpu": "6809", "desc": "SWI (inh) - sets I F E, PC to vector", "code_addr": 0x2000, "code": [0x3F], "initial_regs": {"PC": 0, "S": 0x3000, "CC": 0}, "initial_mem": {0xFFFA: 0x12, 0xFFFB: 0x34}, "expected_regs": {"PC": 0x1234, "S": 0x2FF4, "CC": 0xD0}},
    {"name": "6309_ADDE_imm", "cpu": "6309", "desc": "ADDE #$01 (11 8B)", "code_addr": 0x2000, "code": [0x11, 0x8B, 0x01], "initial_regs": {"E": 0x7F, "CC": 0}, "expected_regs": {"E": 0x80, "PC": 0x2003, "CC": 0x2A}},
    {"name": "6309_LDE_dir", "cpu": "6309", "desc": "LDE <$30 (11 96)", "code_addr": 0x2000, "code": [0x11, 0x96, 0x30], "initial_mem": {0x0030: 0xAB}, "initial_regs": {"E": 0, "CC": 0}, "expected_regs": {"E": 0xAB, "PC": 0x2003, "CC": 0x08}},
    {"name": "6309_AIM_dir", "cpu": "6309", "desc": "AIM #$0F,<$30 (dir bit 6309)", "code_addr": 0x2000, "code": [0x02, 0x0F, 0x30], "initial_mem": {0x0030: 0xFF}, "initial_regs": {"CC": 0}, "expected_regs": {"PC": 0x2003}, "expected_mem": {0x0030: 0x0F}},
    {"name": "6309_TFM_r0r1", "cpu": "6309", "desc": "TFM X+,Y+ (block 6309, 1 byte for test)", "code_addr": 0x2000, "code": [0x11, 0x38], "initial_regs": {"X": 0x3000, "Y": 0x4000}, "initial_mem": {0x3000: 0xAA}, "expected_regs": {"X": 0x3001, "Y": 0x4001, "PC": 0x2002}, "expected_mem": {0x4000: 0xAA}},
    # Additional comprehensive ones for more coverage (every mode for key instr, more 6309)
    {"name": "6809_SUBB_imm", "cpu": "6809", "desc": "SUBB #$01 (imm)", "code_addr": 0x2000, "code": [0xC0, 0x01], "initial_regs": {"B": 0x02, "CC": 0}, "expected_regs": {"B": 0x01, "PC": 0x2002, "CC": 0x00}},
    {"name": "6809_ANDA_imm", "cpu": "6809", "desc": "ANDA #$0F (imm)", "code_addr": 0x2000, "code": [0x84, 0x0F], "initial_regs": {"A": 0xF0, "CC": 0}, "expected_regs": {"A": 0x00, "PC": 0x2002, "CC": 0x04}},
    {"name": "6809_LDB_imm", "cpu": "6809", "desc": "LDB #$AB (imm)", "code_addr": 0x2000, "code": [0xC6, 0xAB], "initial_regs": {"B": 0, "CC": 0}, "expected_regs": {"B": 0xAB, "PC": 0x2002, "CC": 0x08}},
    {"name": "6809_BHI", "cpu": "6809", "desc": "BHI (C|Z==0)", "code_addr": 0x2000, "code": [0x22, 0x02], "initial_regs": {"PC": 0, "CC": 0}, "expected_regs": {"PC": 0x2004}},
    {"name": "6809_BCC", "cpu": "6809", "desc": "BCC (C==0)", "code_addr": 0x2000, "code": [0x24, 0x02], "initial_regs": {"PC": 0, "CC": 0}, "expected_regs": {"PC": 0x2004}},
    {"name": "6809_LDD_imm", "cpu": "6809", "desc": "LDD #$1234 (imm16)", "code_addr": 0x2000, "code": [0xCC, 0x12, 0x34], "initial_regs": {"D": 0, "CC": 0}, "expected_regs": {"D": 0x1234, "PC": 0x2003, "CC": 0}},
    {"name": "6809_PSHU", "cpu": "6809", "desc": "PSHU bit6 pushes S onto U", "code_addr": 0x2000, "code": [0x36, 0x40], "initial_regs": {"U": 0x1234, "S": 0x4000}, "expected_regs": {"U": 0x1232, "S": 0x4000, "PC": 0x2002}},
    {"name": "6809_ADDD_imm", "cpu": "6809", "desc": "ADDD #$0001", "code_addr": 0x2000, "code": [0xC3, 0x00, 0x01], "initial_regs": {"D": 0x0001, "CC": 0}, "expected_regs": {"D": 0x0002, "PC": 0x2003, "CC": 0x00}},
    {"name": "6809_ADDD_wrap", "cpu": "6809", "desc": "ADDD #$0001 from FFFF sets C and Z", "code_addr": 0x2000, "code": [0xC3, 0x00, 0x01], "initial_regs": {"D": 0xFFFF, "CC": 0}, "expected_regs": {"D": 0x0000, "PC": 0x2003, "CC": 0x05}},
    {"name": "6809_IRQ", "cpu": "6809", "desc": "IRQ taken when I is clear", "prelude": ["IRQ"], "code_addr": 0x2000, "code": [0x12], "initial_mem": {0xFFF8: 0x40, 0xFFF9: 0x00}, "initial_regs": {"S": 0x3000, "CC": 0}, "expected_regs": {"PC": 0x4000, "S": 0x2FF4, "CC": 0x90}},
    {"name": "6809_IRQ_masked", "cpu": "6809", "desc": "IRQ pending while I is set", "prelude": ["IRQ"], "code_addr": 0x2000, "code": [0x12], "initial_regs": {"S": 0x3000, "CC": 0x10}, "expected_regs": {"PC": 0x2001, "S": 0x3000, "CC": 0x10}},
    {"name": "6809_FIRQ", "cpu": "6809", "desc": "FIRQ stacks PC and CC only", "prelude": ["FIRQ"], "code_addr": 0x2000, "code": [0x12], "initial_mem": {0xFFF6: 0x41, 0xFFF7: 0x00}, "initial_regs": {"S": 0x3000, "CC": 0}, "expected_regs": {"PC": 0x4100, "S": 0x2FFD, "CC": 0x50}},
    {"name": "6809_FIRQ_masked", "cpu": "6809", "desc": "FIRQ pending while F is set", "prelude": ["FIRQ"], "code_addr": 0x2000, "code": [0x12], "initial_regs": {"S": 0x3000, "CC": 0x40}, "expected_regs": {"PC": 0x2001, "S": 0x3000, "CC": 0x40}},
    {"name": "6809_NMI", "cpu": "6809", "desc": "NMI always stacks the full state", "prelude": ["NMI"], "code_addr": 0x2000, "code": [0x12], "initial_mem": {0xFFFC: 0x42, 0xFFFD: 0x00}, "initial_regs": {"S": 0x3000, "CC": 0}, "expected_regs": {"PC": 0x4200, "S": 0x2FF4, "CC": 0xD0}},
    {"name": "6809_LEAY_idx", "cpu": "6809", "desc": "LEAY 5,Y", "code_addr": 0x2000, "code": [0x31, 0x25], "initial_regs": {"Y": 0x2000}, "expected_regs": {"Y": 0x2005, "PC": 0x2002, "CC": 0}},
    {"name": "6809_LBRA", "cpu": "6809", "desc": "LBRA offset is added after the 3-byte instruction", "code_addr": 0x2000, "code": [0x16, 0x00, 0x05], "initial_regs": {"PC": 0}, "expected_regs": {"PC": 0x2008}},
    {"name": "6309_LDE_imm", "cpu": "6309", "desc": "LDE #$AB (11 86)", "code_addr": 0x2000, "code": [0x11, 0x86, 0xAB], "initial_regs": {"E": 0, "CC": 0}, "expected_regs": {"E": 0xAB, "PC": 0x2003, "CC": 0x08}},
    {"name": "6309_TFM_r0r1_dec", "cpu": "6309", "desc": "TFM X-,Y- ", "code_addr": 0x2000, "code": [0x11, 0x3B], "initial_regs": {"X": 0x3001, "Y": 0x4001}, "initial_mem": {0x3000: 0xBB}, "expected_regs": {"X": 0x3000, "Y": 0x4000, "PC": 0x2002}, "expected_mem": {0x4000: 0xBB}},
    {"name": "6309_ADDF_dir", "cpu": "6309", "desc": "ADDF <$30 (11 DB)", "code_addr": 0x2000, "code": [0x11, 0xDB, 0x30], "initial_mem": {0x0030: 0x01}, "initial_regs": {"F": 0x7F, "CC": 0}, "expected_regs": {"F": 0x80, "PC": 0x2003, "CC": 0x2A}},
]

def main():
    misencode_self_check()
    ap = argparse.ArgumentParser(description="mimic09 instruction suite")
    ap.add_argument("--binary", default=None, help="path to mimic09")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--test", default=None)
    args = ap.parse_args()
    if args.list:
        for t in TESTS:
            print(t["name"])
        return
    binary = find_binary(args.binary)
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
        print(f"binary not executable: {binary}", file=sys.stderr)
        sys.exit(1)
    selected = args.test
    to_run = [t for t in TESTS if t["name"] == selected] if selected else TESTS
    if selected and not to_run:
        print(f"unknown test: {selected}", file=sys.stderr)
        sys.exit(1)
    print("=== Mimic09 Instruction Set Validation Suite ===")
    print(f"Using binary: {binary}")
    print(f"Total tests: {len(to_run)}")
    print()
    passed = 0
    failed = 0
    failed_names = []
    for t in to_run:
        if t.get("cpu") == "6309" and is_misencoded_6309(t["code"]):
            print(f"=== {t['name']} ===")
            print("  FAIL")
            print("    6309 test encodes a 6809 opcode")
            failed += 1
            failed_names.append(t["name"])
            print()
            continue
        print(f"=== {t['name']} ===")
        print(f"  {t['desc']}")
        if "initial_regs" in t:
            print("  Before (intended):", " ".join(f"{k}={v:04X}" if v>255 else f"{k}={v:02X}" for k,v in t["initial_regs"].items()))
        res = run_one_test(t, binary)
        if "error" in res:
            print(f"  ERROR: {res['error']}")
            print("  FAIL")
            failed += 1
            failed_names.append(t["name"])
            continue
        print("  After (actual):", " ".join(f"{k}={v:04X}" if v and v>255 else f"{k}={v:02X}" if v is not None else f"{k}=?" for k,v in res["final"].items()))
        if "expected_regs" in t:
            print("  After (expected):", " ".join(f"{k}={v:04X}" if v>255 else f"{k}={v:02X}" for k,v in t["expected_regs"].items()))
        if res["pass"]:
            print("  PASS")
            passed += 1
        else:
            print("  FAIL")
            for d in res["diffs"]:
                print(f"    {d}")
            failed += 1
            failed_names.append(t["name"])
        print()
    print("=== SUITE SUMMARY ===")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print(f"Total : {passed+failed}")
    if failed_names:
        print("Failed tests:")
        for n in failed_names:
            print(f"  - {n}")
    else:
        print("All tests PASSED!")
    if failed > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
