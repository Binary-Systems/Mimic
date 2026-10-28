#!/usr/bin/env mimic-cc09
/* hello.c - "Hello World" C program demonstrating the Mimic shebang capability.
 *
 * First line is the shebang: when this file is chmod +x and the Mimic/bin
 * is in $PATH, executing ./hello.c (or tests/c/hello.c) automatically:
 *   - "compiles" the C via the cc09 tools (toy emitter + a9/xld here)
 *   - links against the runtime (cstart + chario for getc/putc/putchar ACIA I/O)
 *   - loads the result into mimic09 with --acia 0xF940
 *   - runs it, so printf output (the banner) appears via the emulated ACIA.
 *
 * This fulfills the root AGENTS.md requirement for a Hello World that
 * "compiles via the cc09 compiler and runs via the mimic09 emulator"
 * using getc/putc in assembly that addresses the ACIA, linked with C code.
 *
 * The shebang driver (bin/mimic-cc09) is the added "shebang capability".
 */

int main(void)
{
    printf("Hello, world from shebang C via cc09 + mimic09 ACIA!\r\n");
    return 0;
}
