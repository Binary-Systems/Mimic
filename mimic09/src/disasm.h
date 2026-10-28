/* disasm.h — mimic09 built-in disassembler */

#ifndef MIMIC09_DISASM_H
#define MIMIC09_DISASM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Disassemble one instruction at 'addr' into 'out' (null-terminated).
 * Returns the number of bytes consumed by the instruction.
 */
int disasm(uint16_t addr, char *out, size_t outsz, uint16_t *next_addr);

/* Opcode columns on a monitor line. Five bytes is the longest instruction
 * this disassembler walks: a page-2 indexed op with a 16-bit offset.
 * Shorter instructions are padded so the mnemonic starts at one column. */
#define DISASM_OPCOLS 5

/* Write " HH HH ..." and the mnemonic. Missing bytes up to DISASM_OPCOLS
 * are spaces. A longer instruction prints every byte, then the mnemonic. */
void disasm_opfield(char *out, size_t outsz, uint16_t addr, int nbytes,
                    const char *mnem);

#ifdef __cplusplus
}
#endif
#endif
