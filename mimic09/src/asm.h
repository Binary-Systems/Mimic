/* asm.h — one-line 6809/6309 assembler for the Mimic SuperMon monitor */

#ifndef MIMIC09_ASM_H
#define MIMIC09_ASM_H

#include <stdint.h>
#include <stddef.h>

/* Assemble one instruction at addr. Writes the bytes.
 * Returns the length, or -1 and a message in err. */
int asm_instr(uint16_t addr, const char *text, char *err, size_t errsz);

#endif
