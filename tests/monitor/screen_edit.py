#!/usr/bin/env python3
"""Pty session for the Mimic SuperMon screen editor.

Drives a real terminal: ANSI arrows, SS3 application arrows, and VT52
arrows. A pipe must stay on the line reader and must not emit escapes.
"""

import argparse
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time


def spawn(binary, rows, cols, term):
    master, slave = pty.openpty()
    fcntl_mod = __import__("fcntl")
    winsz = struct.pack("HHHH", rows, cols, 0, 0)
    fcntl_mod.ioctl(slave, termios.TIOCSWINSZ, winsz)
    pid = os.fork()
    if pid == 0:
        os.close(master)
        os.setsid()
        try:
            fcntl_mod.ioctl(slave, termios.TIOCSCTTY, 0)
        except OSError:
            pass
        os.dup2(slave, 0)
        os.dup2(slave, 1)
        os.dup2(slave, 2)
        if slave > 2:
            os.close(slave)
        os.environ["TERM"] = term
        os.environ.pop("LINES", None)
        os.environ.pop("COLUMNS", None)
        os.execv(binary, [binary, "-i"])
    os.close(slave)
    return pid, master


def _drain(master):
    while True:
        ready, _, _ = select.select([master], [], [], 0)
        if not ready:
            return
        try:
            chunk = os.read(master, 8192)
        except OSError:
            return
        if not chunk:
            return


def close_child(pid, master):
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    t0 = time.time()
    while time.time() - t0 < 1.0:
        _drain(master)
        wpid, status = os.waitpid(pid, os.WNOHANG)
        if wpid == pid:
            os.close(master)
            return status
        time.sleep(0.02)
    os.close(master)
    return None


def run_pty(binary, rows, cols, term, script, needle, timeout=4.0):
    """Write the script while reading so a full pty buffer cannot deadlock."""
    pid, master = spawn(binary, rows, cols, term)
    os.set_blocking(master, False)
    data = b""
    pending = b""
    phase_end = time.time() + 3.0
    deadline = phase_end
    started = False
    try:
        while time.time() < deadline:
            # "Exiting" is printed only after the trailing x, so the child has
            # consumed the whole script. An earlier needle would stop the
            # write before the rest of the session ran.
            if started and not pending and b"Exiting mimic09." in data:
                break
            if not started and b"Mimic SuperMon" in data:
                pending = script
                started = True
                deadline = time.time() + timeout
            readable, writable, _ = select.select(
                [master], [master] if pending else [], [], 0.1
            )
            if writable and pending:
                try:
                    n = os.write(master, pending)
                except BlockingIOError:
                    n = 0
                pending = pending[n:]
            if readable:
                try:
                    chunk = os.read(master, 8192)
                except BlockingIOError:
                    chunk = b""
                except OSError:
                    break
                if not chunk:
                    break
                data += chunk
        return data
    finally:
        close_child(pid, master)


def show(data):
    return data.decode("latin1", errors="replace").replace("\x1b", "ESC")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    args = ap.parse_args()
    binary = args.binary
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
        print(f"binary is not executable: {binary}")
        return 1

    fails = []

    def need(ok, msg, data=b""):
        if ok:
            return
        fails.append(msg)
        if data:
            sys.stderr.write(f"--- {msg} ---\n")
            sys.stderr.write(show(data[-2000:]))
            sys.stderr.write("\n")

    # CSI moves onto the first dump byte, SS3 onto the next, VT52 onto the third.
    # The command line has no prompt, so the cursor starts at column 0.
    # Six rights pass '>' and the address and land on the first hex digit.
    # A disassembly mnemonic starts at column 25 (five opcode slots).
    script = b"m 3000\r"
    script += b"\x1b[A" * 12 + b"\x1b[C" * 6 + b"AB"
    script += b"\x1b[B" * 12
    script += b"\x1bOA" * 12 + b"CD"
    script += b"\x1bOB" * 12
    script += b"\x1bA" * 12 + b"EF"
    script += b"\x1bB" * 12
    script += b"m 3000\r"
    script += b"a 3100 nop\r"
    script += b"d 3100 3100\r"
    script += b"\x1b[A" + b"\x1b[C" * 25 + b"LSRA\r"
    script += b"\x1b[B"
    script += b"d 3100 3100\r"
    script += b"a 3200\r" + b"nop\r" + b"\r"
    script += b"d 3200 3200\r"
    script += b"x\r"

    ansi = run_pty(binary, 24, 80, "vt100", script, b">3000 AB CD EF", 5.0)
    need(b"\x1bP" not in ansi, "ANSI session emitted ESC P", ansi)
    need(b">3000 AB CD EF" in ansi, "arrow edit did not reach memory", ansi)
    need(b"LSRA" in ansi and b"44" in ansi, "disasm Return did not assemble LSRA", ansi)
    need(b". 3200 12" in ansi and b"NOP" in ansi, "assemble-until-blank on a tty", ansi)

    vt = run_pty(
        binary,
        24,
        80,
        "vt52",
        b"m 3300\r" + b"\x1bA" * 12 + b"\x1bC" * 6 + b"5A" + b"\x1bB" * 12 + b"m 3300\r" + b"x\r",
        b">3300 5A",
        5.0,
    )
    need(b"\x1bP" not in vt, "VT52 session emitted ESC P", vt)
    need(b"\x1b[" not in vt, "VT52 session used an ANSI control", vt)
    need(b"\x1bY" in vt, "VT52 session did not address the cursor", vt)
    need(b">3300 5A" in vt, "VT52 arrows did not edit memory", vt)

    run = run_pty(
        binary,
        24,
        80,
        "vt100",
        b":2100 13\r" + b"g 2100\r" + b"m 4100\r" + b"! printf 'HI\\n'\r" + b"m 4200\r" + b"x\r",
        b">4200",
        5.0,
    )
    need(b"Halted" in run, "go did not return to the editor", run)
    need(b">4100" in run, "memory command after go failed", run)
    need(b"HI" in run, "shell command produced no output", run)
    need(b">4200" in run, "memory command after shell failed", run)
    need(b"\x1bP" not in run, "go/shell session emitted ESC P", run)

    narrow = run_pty(binary, 24, 20, "vt100", b"m 4000\r" + b"x\r", b">4003", 4.0)
    need(b"\x1bP" not in narrow, "narrow session emitted ESC P", narrow)
    need(b">4003" in narrow, "narrow dump did not step three bytes", narrow)
    need(b">4000 00 00 00:" in narrow, "narrow dump was not three bytes", narrow)
    need(b">4008" not in narrow, "narrow dump kept an 8-byte stride", narrow)

    # Up from the blank command line is the value line. Column 0 is ';'.
    # One right is the first PC digit. 12AB overtypes PC, then r reprints it.
    regs = run_pty(
        binary,
        24,
        80,
        "vt100",
        b"\x1b[A" + b"\x1b[C" + b"12AB" + b"\x1b[B" + b"r\r" + b"x\r",
        b";12AB ",
        4.0,
    )
    need(b"\x1bP" not in regs, "register edit emitted ESC P", regs)
    need(b" PC   CC EFHINZVC " in regs, "register names were not drawn", regs)
    need(b";12AB " in regs, "arrow edit did not change PC", regs)
    need(b"-- --" in regs and b"----" in regs, "emulation mode hid unread 6309 registers", regs)

    pipe = subprocess.run(
        [binary, "-i"],
        input="m 2000\nx\n",
        text=True,
        capture_output=True,
        timeout=5,
    )
    pipe_out = pipe.stdout + pipe.stderr
    need("\x1b" not in pipe_out, "a pipe emitted an escape", pipe_out.encode())
    need(">2000 00 00 00 00 00 00 00 00:" in pipe_out, "a pipe lost the 8-byte dump")
    need(pipe.returncode == 0, f"pipe exit {pipe.returncode}")

    if fails:
        print(f"FAIL {len(fails)}")
        for msg in fails:
            print(f"  {msg}")
        return 1
    print("PASS screen editor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
