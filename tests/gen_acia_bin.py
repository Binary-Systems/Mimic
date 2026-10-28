#!/usr/bin/env python3
"""Generate a raw 6809 binary that writes a banner to ACIA at $F940 for Mimic ACIA tests.
This is a temporary bridge until a9 supports ST*/FCB and full extended stores.
The source in acia-hello.asm is the intended cc09 input.
"""
import sys

def main():
    base = 0x2000
    # Unrolled: LDS(4) + 5*(LDB2 + JSR3=5) = 4+25=29 + BRA2 = 31 bytes for main code
    # Place outch at 0x30 (safe after)
    outch_addr = base + 0x30
    c = bytearray()
    c += bytes([0x10, 0xCE, 0x30, 0x00])  # LDS #$3000   @2000
    for ch in [ord("H"), ord("I"), ord("!"), 0x0D, 0x0A]:
        c += bytes([0xC6, ch])            # LDB #ch
        c += bytes([0xBD, (outch_addr >> 8) & 0xFF, outch_addr & 0xFF])  # JSR outch
    c += bytes([0x20, 0xFE])              # BRA *        @ ~0x2000+29
    # pad up to outch
    while len(c) < 0x30:
        c.append(0x12)  # NOP
    # outch: @ base+0x30
    c += bytes([0x34, 0x02])              # PSHS A
    c += bytes([0x86, 0x02])              # LDA #2
    c += bytes([0xB5, 0xF9, 0x40])        # BITA $F940
    c += bytes([0x27, 0xFB])              # BEQ *-5
    c += bytes([0xF7, 0xF9, 0x41])        # STB $F941
    c += bytes([0x35, 0x02])              # PULS A
    c += bytes([0x39])                    # RTS
    outp = sys.argv[1] if len(sys.argv) > 1 else "tmp/acia-hello.bin"
    with open(outp, "wb") as f:
        f.write(c)
    print("Wrote %s (%d bytes) load@%04X entry@%04X outch@%04X" % (outp, len(c), base, base, outch_addr))

if __name__ == "__main__":
    main()
