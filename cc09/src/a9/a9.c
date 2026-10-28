/* a9.c — Wintek 6809 assembler, relocatable S-record objects.
 * Hitachi 6309 mnemonics use the Hitachi opcodes. CLRD stays the
 * 6809 clear of D (CLRB, CLRA) unless the source says OPT 6309.
 *
 * Two passes. Symbols survive pass 2; only section PCs reset.
 * Object layout matches the golden cstart/chario records:
 *   S0 name, S4 on section close, S8 at each GLOBAL definition,
 *   S2 chunks (C0/C1/C2 relocs), S7 at each external fixup,
 *   SD 0208 when a non-PSCT section was emitted, S9 at address 0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "srec.h"
#include "util.h"

static const char *VERSION = "3.1r0";
static const char *COPYRIGHT = "Copyright 2026 Binary Systems, Inc.";

#define MAXSYM  4096
#define MAXNAME 32
#define MAXCHUNK 1024
#define MAXTOK  256
#define MAXMETA 2048
#define MAXLINE 1024
#define MAX_S2  33
#define MAXSEC  16
#define MAXMAC  32
#define MAXMACL 64
#define LISTCAP 16

/* Ids 0..4 are reserved (4 is unused CSCT). Named sections start at 5. */
enum { SEC_ASCT = 0, SEC_DSCT = 1, SEC_PSCT = 2, SEC_BSCT = 3, SEC_BASE = 5 };
enum { K_ABS = 0, K_REL = 1, K_EXT = 2, K_UNDEF = 3 };
enum { TK_L, TK_C1, TK_C2, TK_EXT, TK_D8 };
enum { K_INH, K_IMM8, K_IMM16, K_MEM, K_RELB, K_STK, K_LEA, K_TFR, K_LREL,
       K_IMM32, K_IMMEA, K_BIT, K_TFM };

typedef struct {
    const char *mnem;
    int kind, page;
    int oi, od, ox, oe; /* -1 = none */
} opc_t;

static const opc_t OPS[] = {
    {"NOP", K_INH, 0, 0x12, -1, -1, -1},
    {"SYNC", K_INH, 0, 0x13, -1, -1, -1},
    {"RTS", K_INH, 0, 0x39, -1, -1, -1},
    {"ABX", K_INH, 0, 0x3A, -1, -1, -1},
    {"MUL", K_INH, 0, 0x3D, -1, -1, -1},
    {"DAA", K_INH, 0, 0x19, -1, -1, -1},
    {"SEX", K_INH, 0, 0x1D, -1, -1, -1},
    {"CLRA", K_INH, 0, 0x4F, -1, -1, -1},
    {"CLRB", K_INH, 0, 0x5F, -1, -1, -1},
    {"TSTA", K_INH, 0, 0x4D, -1, -1, -1},
    {"TSTB", K_INH, 0, 0x5D, -1, -1, -1},
    {"LSRA", K_INH, 0, 0x44, -1, -1, -1},
    {"LSRB", K_INH, 0, 0x54, -1, -1, -1},
    {"COMA", K_INH, 0, 0x43, -1, -1, -1},
    {"COMB", K_INH, 0, 0x53, -1, -1, -1},
    {"INCA", K_INH, 0, 0x4C, -1, -1, -1},
    {"INCB", K_INH, 0, 0x5C, -1, -1, -1},
    {"DECA", K_INH, 0, 0x4A, -1, -1, -1},
    {"DECB", K_INH, 0, 0x5A, -1, -1, -1},
    {"ASLA", K_INH, 0, 0x48, -1, -1, -1},
    {"ASLB", K_INH, 0, 0x58, -1, -1, -1},
    {"ASRA", K_INH, 0, 0x47, -1, -1, -1},
    {"ASRB", K_INH, 0, 0x57, -1, -1, -1},
    {"ROLA", K_INH, 0, 0x49, -1, -1, -1},
    {"ROLB", K_INH, 0, 0x59, -1, -1, -1},
    {"RORA", K_INH, 0, 0x46, -1, -1, -1},
    {"RORB", K_INH, 0, 0x56, -1, -1, -1},
    {"NEGA", K_INH, 0, 0x40, -1, -1, -1},
    {"NEGB", K_INH, 0, 0x50, -1, -1, -1},
    {"LDA", K_MEM, 0, 0x86, 0x96, 0xA6, 0xB6},
    {"LDB", K_MEM, 0, 0xC6, 0xD6, 0xE6, 0xF6},
    {"LDD", K_IMM16, 0, 0xCC, 0xDC, 0xEC, 0xFC},
    {"LDX", K_IMM16, 0, 0x8E, 0x9E, 0xAE, 0xBE},
    {"LDU", K_IMM16, 0, 0xCE, 0xDE, 0xEE, 0xFE},
    {"STA", K_MEM, 0, -1, 0x97, 0xA7, 0xB7},
    {"STB", K_MEM, 0, -1, 0xD7, 0xE7, 0xF7},
    {"STD", K_MEM, 0, -1, 0xDD, 0xED, 0xFD},
    {"STX", K_MEM, 0, -1, 0x9F, 0xAF, 0xBF},
    {"STU", K_MEM, 0, -1, 0xDF, 0xEF, 0xFF},
    {"ADDA", K_MEM, 0, 0x8B, 0x9B, 0xAB, 0xBB},
    {"ADDB", K_MEM, 0, 0xCB, 0xDB, 0xEB, 0xFB},
    {"SUBA", K_MEM, 0, 0x80, 0x90, 0xA0, 0xB0},
    {"SUBB", K_MEM, 0, 0xC0, 0xD0, 0xE0, 0xF0},
    {"SUBD", K_IMM16, 0, 0x83, 0x93, 0xA3, 0xB3},
    {"ADDD", K_IMM16, 0, 0xC3, 0xD3, 0xE3, 0xF3},
    {"CMPA", K_MEM, 0, 0x81, 0x91, 0xA1, 0xB1},
    {"CMPB", K_MEM, 0, 0xC1, 0xD1, 0xE1, 0xF1},
    {"ANDA", K_MEM, 0, 0x84, 0x94, 0xA4, 0xB4},
    {"ANDB", K_MEM, 0, 0xC4, 0xD4, 0xE4, 0xF4},
    {"BITA", K_MEM, 0, 0x85, 0x95, 0xA5, 0xB5},
    {"BITB", K_MEM, 0, 0xC5, 0xD5, 0xE5, 0xF5},
    {"EORA", K_MEM, 0, 0x88, 0x98, 0xA8, 0xB8},
    {"EORB", K_MEM, 0, 0xC8, 0xD8, 0xE8, 0xF8},
    {"ORA", K_MEM, 0, 0x8A, 0x9A, 0xAA, 0xBA},
    {"ORB", K_MEM, 0, 0xCA, 0xDA, 0xEA, 0xFA},
    {"ADCA", K_MEM, 0, 0x89, 0x99, 0xA9, 0xB9},
    {"ADCB", K_MEM, 0, 0xC9, 0xD9, 0xE9, 0xF9},
    {"SBCA", K_MEM, 0, 0x82, 0x92, 0xA2, 0xB2},
    {"SBCB", K_MEM, 0, 0xC2, 0xD2, 0xE2, 0xF2},
    {"JSR", K_MEM, 0, -1, 0x9D, 0xAD, 0xBD},
    {"JMP", K_MEM, 0, -1, 0x0E, 0x6E, 0x7E},
    {"LDS", K_IMM16, 0x10, 0xCE, 0xDE, 0xEE, 0xFE},
    {"LDY", K_IMM16, 0x10, 0x8E, 0x9E, 0xAE, 0xBE},
    {"STS", K_MEM, 0x10, -1, 0xDF, 0xEF, 0xFF},
    {"STY", K_MEM, 0x10, -1, 0x9F, 0xAF, 0xBF},
    {"CMPD", K_IMM16, 0x10, 0x83, 0x93, 0xA3, 0xB3},
    {"CMPX", K_IMM16, 0, 0x8C, 0x9C, 0xAC, 0xBC},
    {"CMPY", K_IMM16, 0x10, 0x8C, 0x9C, 0xAC, 0xBC},
    {"CMPU", K_IMM16, 0x11, 0x83, 0x93, 0xA3, 0xB3},
    {"CMPS", K_IMM16, 0x11, 0x8C, 0x9C, 0xAC, 0xBC},
    {"BSR", K_RELB, 0, 0x8D, -1, -1, -1},
    {"BRA", K_RELB, 0, 0x20, -1, -1, -1},
    {"BRN", K_RELB, 0, 0x21, -1, -1, -1},
    {"BHI", K_RELB, 0, 0x22, -1, -1, -1},
    {"BLS", K_RELB, 0, 0x23, -1, -1, -1},
    {"BCC", K_RELB, 0, 0x24, -1, -1, -1},
    {"BHS", K_RELB, 0, 0x24, -1, -1, -1},
    {"BCS", K_RELB, 0, 0x25, -1, -1, -1},
    {"BLO", K_RELB, 0, 0x25, -1, -1, -1},
    {"BNE", K_RELB, 0, 0x26, -1, -1, -1},
    {"BEQ", K_RELB, 0, 0x27, -1, -1, -1},
    {"BVC", K_RELB, 0, 0x28, -1, -1, -1},
    {"BVS", K_RELB, 0, 0x29, -1, -1, -1},
    {"BPL", K_RELB, 0, 0x2A, -1, -1, -1},
    {"BMI", K_RELB, 0, 0x2B, -1, -1, -1},
    {"BGE", K_RELB, 0, 0x2C, -1, -1, -1},
    {"BLT", K_RELB, 0, 0x2D, -1, -1, -1},
    {"BGT", K_RELB, 0, 0x2E, -1, -1, -1},
    {"BLE", K_RELB, 0, 0x2F, -1, -1, -1},
    {"PSHS", K_STK, 0, 0x34, -1, -1, -1},
    {"PULS", K_STK, 0, 0x35, -1, -1, -1},
    {"PSHU", K_STK, 0, 0x36, -1, -1, -1},
    {"PULU", K_STK, 0, 0x37, -1, -1, -1},
    {"LEAX", K_LEA, 0, 0x30, -1, -1, -1},
    {"LEAY", K_LEA, 0, 0x31, -1, -1, -1},
    {"LEAS", K_LEA, 0, 0x32, -1, -1, -1},
    {"LEAU", K_LEA, 0, 0x33, -1, -1, -1},
    {"RTI", K_INH, 0, 0x3B, -1, -1, -1},
    {"SWI", K_INH, 0, 0x3F, -1, -1, -1},
    {"SWI2", K_INH, 0x10, 0x3F, -1, -1, -1},
    {"SWI3", K_INH, 0x11, 0x3F, -1, -1, -1},
    {"ANDCC", K_IMM8, 0, 0x1C, -1, -1, -1},
    {"ORCC", K_IMM8, 0, 0x1A, -1, -1, -1},
    {"CWAI", K_IMM8, 0, 0x3C, -1, -1, -1},
    {"TFR", K_TFR, 0, 0x1F, -1, -1, -1},
    {"EXG", K_TFR, 0, 0x1E, -1, -1, -1},
    {"LBRA", K_LREL, 0, 0x16, -1, -1, -1},
    {"LBSR", K_LREL, 0, 0x17, -1, -1, -1},
    {"LBRN", K_LREL, 0x10, 0x21, -1, -1, -1},
    {"LBHI", K_LREL, 0x10, 0x22, -1, -1, -1},
    {"LBLS", K_LREL, 0x10, 0x23, -1, -1, -1},
    {"LBCC", K_LREL, 0x10, 0x24, -1, -1, -1},
    {"LBHS", K_LREL, 0x10, 0x24, -1, -1, -1},
    {"LBCS", K_LREL, 0x10, 0x25, -1, -1, -1},
    {"LBLO", K_LREL, 0x10, 0x25, -1, -1, -1},
    {"LBNE", K_LREL, 0x10, 0x26, -1, -1, -1},
    {"LBEQ", K_LREL, 0x10, 0x27, -1, -1, -1},
    {"LBVC", K_LREL, 0x10, 0x28, -1, -1, -1},
    {"LBVS", K_LREL, 0x10, 0x29, -1, -1, -1},
    {"LBPL", K_LREL, 0x10, 0x2A, -1, -1, -1},
    {"LBMI", K_LREL, 0x10, 0x2B, -1, -1, -1},
    {"LBGE", K_LREL, 0x10, 0x2C, -1, -1, -1},
    {"LBLT", K_LREL, 0x10, 0x2D, -1, -1, -1},
    {"LBGT", K_LREL, 0x10, 0x2E, -1, -1, -1},
    {"LBLE", K_LREL, 0x10, 0x2F, -1, -1, -1},
    {"NEG", K_MEM, 0, -1, 0x00, 0x60, 0x70},
    {"COM", K_MEM, 0, -1, 0x03, 0x63, 0x73},
    {"LSR", K_MEM, 0, -1, 0x04, 0x64, 0x74},
    {"ROR", K_MEM, 0, -1, 0x06, 0x66, 0x76},
    {"ASR", K_MEM, 0, -1, 0x07, 0x67, 0x77},
    {"ASL", K_MEM, 0, -1, 0x08, 0x68, 0x78},
    {"LSL", K_MEM, 0, -1, 0x08, 0x68, 0x78},
    {"ROL", K_MEM, 0, -1, 0x09, 0x69, 0x79},
    {"DEC", K_MEM, 0, -1, 0x0A, 0x6A, 0x7A},
    {"INC", K_MEM, 0, -1, 0x0C, 0x6C, 0x7C},
    {"TST", K_MEM, 0, -1, 0x0D, 0x6D, 0x7D},
    {"CLR", K_MEM, 0, -1, 0x0F, 0x6F, 0x7F},
    /* Hitachi 6309. Page is the prefix. oi/od/ox/oe are imm/dir/idx/ext.
     * LDQ immediate is CD and four data bytes; its memory forms use page 10. */
    {"SEXW", K_INH, 0, 0x14, -1, -1, -1},
    {"LDQ", K_IMM32, 0x10, 0xCD, 0xDC, 0xEC, 0xFC},
    {"STQ", K_MEM, 0x10, -1, 0xDD, 0xED, 0xFD},
    {"OIM", K_IMMEA, 0, -1, 0x01, 0x61, 0x71},
    {"AIM", K_IMMEA, 0, -1, 0x02, 0x62, 0x72},
    {"EIM", K_IMMEA, 0, -1, 0x05, 0x65, 0x75},
    {"TIM", K_IMMEA, 0, -1, 0x0B, 0x6B, 0x7B},
    {"ADDR", K_TFR, 0x10, 0x30, -1, -1, -1},
    {"ADCR", K_TFR, 0x10, 0x31, -1, -1, -1},
    {"SUBR", K_TFR, 0x10, 0x32, -1, -1, -1},
    {"SBCR", K_TFR, 0x10, 0x33, -1, -1, -1},
    {"ANDR", K_TFR, 0x10, 0x34, -1, -1, -1},
    {"ORR", K_TFR, 0x10, 0x35, -1, -1, -1},
    {"EORR", K_TFR, 0x10, 0x36, -1, -1, -1},
    {"CMPR", K_TFR, 0x10, 0x37, -1, -1, -1},
    {"PSHSW", K_INH, 0x10, 0x38, -1, -1, -1},
    {"PULSW", K_INH, 0x10, 0x39, -1, -1, -1},
    {"PSHUW", K_INH, 0x10, 0x3A, -1, -1, -1},
    {"PULUW", K_INH, 0x10, 0x3B, -1, -1, -1},
    {"NEGD", K_INH, 0x10, 0x40, -1, -1, -1},
    {"COMD", K_INH, 0x10, 0x43, -1, -1, -1},
    {"LSRD", K_INH, 0x10, 0x44, -1, -1, -1},
    {"RORD", K_INH, 0x10, 0x46, -1, -1, -1},
    {"ASRD", K_INH, 0x10, 0x47, -1, -1, -1},
    {"LSLD", K_INH, 0x10, 0x48, -1, -1, -1},
    {"ROLD", K_INH, 0x10, 0x49, -1, -1, -1},
    {"DECD", K_INH, 0x10, 0x4A, -1, -1, -1},
    {"INCD", K_INH, 0x10, 0x4C, -1, -1, -1},
    {"TSTD", K_INH, 0x10, 0x4D, -1, -1, -1},
    {"CLRD", K_INH, 0x10, 0x4F, -1, -1, -1},
    {"COMW", K_INH, 0x10, 0x53, -1, -1, -1},
    {"LSRW", K_INH, 0x10, 0x54, -1, -1, -1},
    {"RORW", K_INH, 0x10, 0x56, -1, -1, -1},
    {"ROLW", K_INH, 0x10, 0x59, -1, -1, -1},
    {"DECW", K_INH, 0x10, 0x5A, -1, -1, -1},
    {"INCW", K_INH, 0x10, 0x5C, -1, -1, -1},
    {"TSTW", K_INH, 0x10, 0x5D, -1, -1, -1},
    {"CLRW", K_INH, 0x10, 0x5F, -1, -1, -1},
    {"SUBW", K_IMM16, 0x10, 0x80, 0x90, 0xA0, 0xB0},
    {"CMPW", K_IMM16, 0x10, 0x81, 0x91, 0xA1, 0xB1},
    {"SBCD", K_IMM16, 0x10, 0x82, 0x92, 0xA2, 0xB2},
    {"ANDD", K_IMM16, 0x10, 0x84, 0x94, 0xA4, 0xB4},
    {"BITD", K_IMM16, 0x10, 0x85, 0x95, 0xA5, 0xB5},
    {"LDW", K_IMM16, 0x10, 0x86, 0x96, 0xA6, 0xB6},
    {"STW", K_MEM, 0x10, -1, 0x97, 0xA7, 0xB7},
    {"EORD", K_IMM16, 0x10, 0x88, 0x98, 0xA8, 0xB8},
    {"ADCD", K_IMM16, 0x10, 0x89, 0x99, 0xA9, 0xB9},
    {"ORD", K_IMM16, 0x10, 0x8A, 0x9A, 0xAA, 0xBA},
    {"ADDW", K_IMM16, 0x10, 0x8B, 0x9B, 0xAB, 0xBB},
    {"BAND", K_BIT, 0x11, 0x30, -1, -1, -1},
    {"BIAND", K_BIT, 0x11, 0x31, -1, -1, -1},
    {"BOR", K_BIT, 0x11, 0x32, -1, -1, -1},
    {"BIOR", K_BIT, 0x11, 0x33, -1, -1, -1},
    {"BEOR", K_BIT, 0x11, 0x34, -1, -1, -1},
    {"BIEOR", K_BIT, 0x11, 0x35, -1, -1, -1},
    {"LDBT", K_BIT, 0x11, 0x36, -1, -1, -1},
    {"STBT", K_BIT, 0x11, 0x37, -1, -1, -1},
    {"TFM", K_TFM, 0x11, -1, -1, -1, -1},
    {"BITMD", K_IMM8, 0x11, 0x3C, -1, -1, -1},
    {"LDMD", K_IMM8, 0x11, 0x3D, -1, -1, -1},
    {"COME", K_INH, 0x11, 0x43, -1, -1, -1},
    {"DECE", K_INH, 0x11, 0x4A, -1, -1, -1},
    {"INCE", K_INH, 0x11, 0x4C, -1, -1, -1},
    {"TSTE", K_INH, 0x11, 0x4D, -1, -1, -1},
    {"CLRE", K_INH, 0x11, 0x4F, -1, -1, -1},
    {"COMF", K_INH, 0x11, 0x53, -1, -1, -1},
    {"DECF", K_INH, 0x11, 0x5A, -1, -1, -1},
    {"INCF", K_INH, 0x11, 0x5C, -1, -1, -1},
    {"TSTF", K_INH, 0x11, 0x5D, -1, -1, -1},
    {"CLRF", K_INH, 0x11, 0x5F, -1, -1, -1},
    {"SUBE", K_MEM, 0x11, 0x80, 0x90, 0xA0, 0xB0},
    {"CMPE", K_MEM, 0x11, 0x81, 0x91, 0xA1, 0xB1},
    {"LDE", K_MEM, 0x11, 0x86, 0x96, 0xA6, 0xB6},
    {"STE", K_MEM, 0x11, -1, 0x97, 0xA7, 0xB7},
    {"ADDE", K_MEM, 0x11, 0x8B, 0x9B, 0xAB, 0xBB},
    {"DIVD", K_MEM, 0x11, 0x8D, 0x9D, 0xAD, 0xBD},
    {"DIVQ", K_IMM16, 0x11, 0x8E, 0x9E, 0xAE, 0xBE},
    {"MULD", K_IMM16, 0x11, 0x8F, 0x9F, 0xAF, 0xBF},
    {"SUBF", K_MEM, 0x11, 0xC0, 0xD0, 0xE0, 0xF0},
    {"CMPF", K_MEM, 0x11, 0xC1, 0xD1, 0xE1, 0xF1},
    {"LDF", K_MEM, 0x11, 0xC6, 0xD6, 0xE6, 0xF6},
    {"STF", K_MEM, 0x11, -1, 0xD7, 0xE7, 0xF7},
    {"ADDF", K_MEM, 0x11, 0xCB, 0xDB, 0xEB, 0xFB},
};
static const int NOPS = (int)(sizeof(OPS) / sizeof(OPS[0]));

typedef struct {
    char name[MAXNAME];
    int value, kind, sec, defined, is_set;
} sym_t;

typedef struct {
    int pc, size, org, abs, used;
    int span_start, span_open, announced;
    /* Set when the source names the section. A bare ORG is an absolute
     * image and stays clear, so its bytes can be raw S1 records. */
    int explicit;
    char name[MAXNAME];
} sec_t;

typedef struct {
    int k, a, b;
    char name[MAXNAME];
} tok_t;

typedef struct {
    int sec, off, ntok, closed_by_ext, abs;
    tok_t toks[MAXTOK];
} chunk_t;

typedef struct {
    int kind; /* 0 rel S4, 1 abs S4, 2 S8 */
    int sec, end, org, off;
    char name[MAXNAME];
} meta_t;

typedef struct {
    int value, kind, sec;
    char name[MAXNAME];
} val_t;

typedef struct {
    char name[MAXNAME];
    char body[MAXMACL][MAXLINE];
    int nline;
} mac_t;

static sym_t syms[MAXSYM];
static int nsym;
static char globals[MAXSYM][MAXNAME];
static int nglob;
static char externs[MAXSYM][MAXNAME];
static int nextn;
static sec_t secs[MAXSEC];
static int nsec = SEC_BASE;
static int cur = -1;
/* PC at the start of the current statement. `*` keeps this value
 * after the opcode byte has already been emitted (`BNE *-1`). */
static int star_valid, star_pc, star_abs, star_sec;
/* High byte of the direct-page window. SETDP $84 selects $8400-$84FF. */
static int direct_page;
/* Set by OPT 6309. Default CLRD is CLRB then CLRA, which clrio.asm uses. */
static int cpu_6309;
static int pass = 1;
static int errors;
static char module[64] = "module";
static meta_t metas[MAXMETA];
static int nmeta;
static chunk_t *chunks;
static chunk_t *split_hold;
static int nchunk;
static chunk_t *ck;
static FILE *lstf;
static char curpath[512];
static int list_on = 1;
static int list_n, list_mark, list_mark_at, list_equ, list_equ_v;
static int list_before, list_plain, list_force_show, list_hide, list_pc;
static uint8_t list_b[LISTCAP];
static int ifsp;
static struct { int parent_on, taken, saw_else; } ifs[32];
static mac_t macros[MAXMAC];
static int nmac, mac_def = -1, mac_skip, mac_depth, mac_narg;
static char exe_directory[512];
static int have_exe_dir;
static char mac_argline[512];
static int mac_invoke = -1;

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s input-files [-l listing-file] [-o output-file] [-nh]\n", argv0);
    exit(1);
}

static int ieq(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

static void add_name(char arr[][MAXNAME], int *n, const char *s) {
    for (int i = 0; i < *n; i++) if (strcmp(arr[i], s) == 0) return;
    if (*n >= MAXSYM) { errors++; return; }
    strncpy(arr[*n], s, MAXNAME - 1);
    arr[*n][MAXNAME - 1] = 0;
    (*n)++;
}

static int has_name(char arr[][MAXNAME], int n, const char *s) {
    for (int i = 0; i < n; i++) if (strcmp(arr[i], s) == 0) return 1;
    return 0;
}

static void open_span(int sec);
static void ensure_sec(void);

static sec_t *S(void) {
    /* Do not call switch_sec here. It flushes the chunk begin_ck just opened. */
    if (cur < 0 || cur >= MAXSEC) {
        cur = SEC_PSCT;
        if (!secs[cur].span_open) open_span(cur);
    }
    return &secs[cur];
}

static void snap_star(void) {
    if (cur < 0 || cur >= MAXSEC) { star_valid = 0; return; }
    star_valid = 1;
    star_pc = secs[cur].pc & 0xFFFF;
    star_abs = secs[cur].abs;
    star_sec = cur;
}

/* Location counter. A statement's `*` is its first byte, not the PC
 * after an opcode already emitted on this line. */
static val_t loc_counter(void) {
    val_t v;
    memset(&v, 0, sizeof(v));
    if (star_valid) {
        v.value = star_pc & 0xFFFF;
        if (star_abs) { v.kind = K_ABS; v.sec = -1; }
        else { v.kind = K_REL; v.sec = star_sec; }
        return v;
    }
    v.value = (cur >= 0 && cur < MAXSEC) ? (secs[cur].pc & 0xFFFF) : 0;
    if (cur >= 0 && cur < MAXSEC && secs[cur].abs) {
        v.kind = K_ABS;
        v.sec = -1;
    } else {
        v.kind = K_REL;
        v.sec = cur;
    }
    return v;
}

static void add_pc(int n) {
    ensure_sec();
    sec_t *s = S();
    s->pc = (s->pc + n) & 0xFFFF;
    if (pass != 1) return;
    if (!s->abs) {
        if (s->pc > s->size) s->size = s->pc;
    } else if (s->org >= 0) {
        int span = (s->pc - s->org) & 0xFFFF;
        if (span > s->size) s->size = span;
    }
}

static void define_sym(const char *name, int value, int kind, int sec) {
    if (pass != 1) return;
    for (int i = 0; i < nsym; i++) {
        if (strcmp(syms[i].name, name) == 0) {
            if (syms[i].defined &&
                syms[i].value == (value & 0xFFFF) &&
                syms[i].kind == kind && syms[i].sec == sec)
                return;
            if (syms[i].defined) {
                fprintf(stderr, "label doubly defined: %s\n", name);
                errors++;
            }
            return;
        }
    }
    if (nsym >= MAXSYM) { errors++; fprintf(stderr, "symbol table full\n"); return; }
    sym_t *sy = &syms[nsym++];
    memset(sy, 0, sizeof(*sy));
    strncpy(sy->name, name, MAXNAME - 1);
    sy->value = value & 0xFFFF;
    sy->kind = kind;
    sy->sec = sec;
    sy->defined = 1;
}

/* SET is reapplied on both passes. Pass 2 replays from zero. */
static void set_sym(const char *name, int value, int kind, int sec) {
    for (int i = 0; i < nsym; i++) {
        if (strcmp(syms[i].name, name) == 0) {
            syms[i].value = value & 0xFFFF;
            syms[i].kind = kind;
            syms[i].sec = sec;
            syms[i].defined = 1;
            syms[i].is_set = 1;
            return;
        }
    }
    if (nsym >= MAXSYM) { errors++; fprintf(stderr, "symbol table full\n"); return; }
    sym_t *sy = &syms[nsym++];
    memset(sy, 0, sizeof(*sy));
    strncpy(sy->name, name, MAXNAME - 1);
    sy->value = value & 0xFFFF;
    sy->kind = kind;
    sy->sec = sec;
    sy->defined = 1;
    sy->is_set = 1;
}

static val_t lookup(const char *name) {
    val_t v;
    memset(&v, 0, sizeof(v));
    strncpy(v.name, name, MAXNAME - 1);
    /* NARG is the argument count of the macro being expanded. */
    if (mac_depth > 0 && ieq(name, "NARG")) {
        v.value = mac_narg;
        v.kind = K_ABS;
        v.sec = -1;
        return v;
    }
    for (int i = 0; i < nsym; i++) {
        if (strcmp(syms[i].name, name) == 0) {
            v.value = syms[i].value;
            v.kind = syms[i].kind;
            v.sec = syms[i].sec;
            return v;
        }
    }
    if (has_name(externs, nextn, name) || pass == 2)
        v.kind = K_EXT;
    else
        v.kind = K_UNDEF;
    return v;
}

static int parse_num(const char *tok, int *out) {
    if (tok[0] == '%') {
        int v = 0;
        if (!tok[1]) return 0;
        for (const char *p = tok + 1; *p; p++) {
            if (*p != '0' && *p != '1') return 0;
            v = (v << 1) | (*p - '0');
        }
        *out = v;
        return 1;
    }
    if (tok[0] == '$') { *out = (int)strtol(tok + 1, NULL, 16); return 1; }
    if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) { *out = (int)strtol(tok, NULL, 16); return 1; }
    size_t n = strlen(tok);
    if (n >= 2 && (tok[n - 1] == 'h' || tok[n - 1] == 'H') && isdigit((unsigned char)tok[0])) {
        int hex = 1;
        for (size_t i = 0; i < n - 1; i++) if (!isxdigit((unsigned char)tok[i])) hex = 0;
        if (hex) {
            char buf[64];
            if (n >= sizeof(buf)) return 0;
            memcpy(buf, tok, n - 1);
            buf[n - 1] = 0;
            *out = (int)strtol(buf, NULL, 16);
            return 1;
        }
    }
    if (isdigit((unsigned char)tok[0]) || (tok[0] == '-' && isdigit((unsigned char)tok[1]))) {
        *out = atoi(tok);
        return 1;
    }
    return 0;
}

static val_t eval_term(const char *t) {
    val_t v;
    memset(&v, 0, sizeof(v));
    while (*t && isspace((unsigned char)*t)) t++;
    if (t[0] == '*' && t[1] == 0) return loc_counter();
    /* 'X' and the WINTEK short form 'X (no closing quote), as in #': and #'0. */
    if ((t[0] == '\'' || t[0] == '"') && t[1] &&
        (t[2] == 0 || (t[2] == t[0] && t[3] == 0))) {
        v.value = (unsigned char)t[1];
        v.kind = K_ABS;
        return v;
    }
    int num;
    if (parse_num(t, &num)) {
        v.value = num & 0xFFFF;
        v.kind = K_ABS;
        return v;
    }
    return lookup(t);
}

/* * and / bind tighter than + and -. A bare * is the location counter.
 * An absolute addend is kept. The result kind comes from the
 * relocatable term, so 3*4+TQ$ is TQ$ plus 12. */
static void expr_skip(const char **pp) {
    while (**pp && isspace((unsigned char)**pp)) (*pp)++;
}

static val_t eval_add(const char **pp);

static val_t eval_primary(const char **pp) {
    expr_skip(pp);
    const char *p = *pp;
    if (*p == '(') {
        *pp = p + 1;
        val_t v = eval_add(pp);
        expr_skip(pp);
        if (**pp == ')') (*pp)++;
        return v;
    }
    if (*p == '+') { *pp = p + 1; return eval_primary(pp); }
    if (*p == '-' && p[1] != 0) {
        /* A leading minus is unary. A minus between terms is binary. */
        char n = p[1];
        if (n == '(' || n == '*' || n == '+' || n == '-' ||
            isalnum((unsigned char)n) || n == '$' || n == '%' || n == '\'' || n == '"') {
            *pp = p + 1;
            val_t v = eval_primary(pp);
            v.value = (-v.value) & 0xFFFF;
            return v;
        }
    }
    if (*p == '*') {
        char n = p[1];
        if (n == 0 || isspace((unsigned char)n) || n == '+' || n == '-' ||
            n == '*' || n == '/' || n == ')' ) {
            *pp = p + 1;
            return loc_counter();
        }
    }
    char tok[128];
    int i = 0;
    /* 'X' with a closing quote, or the WINTEK short form 'X.
     * The character may be an operator: #'/  #'*  #'- . */
    if (*p == '\'' || *p == '"') {
        char q = *p;
        if (p[1] && p[2] == q) {
            tok[0] = q;
            tok[1] = p[1];
            tok[2] = q;
            tok[3] = 0;
            *pp = p + 3;
            return eval_term(tok);
        }
        if (p[1]) {
            tok[0] = q;
            tok[1] = p[1];
            tok[2] = 0;
            *pp = p + 2;
            return eval_term(tok);
        }
    }
    while (*p && !isspace((unsigned char)*p) &&
           *p != '+' && *p != '-' && *p != '*' && *p != '/' &&
           *p != '(' && *p != ')' && i + 1 < (int)sizeof(tok))
        tok[i++] = *p++;
    tok[i] = 0;
    *pp = p;
    if (!tok[0]) {
        val_t v;
        memset(&v, 0, sizeof(v));
        v.kind = K_ABS;
        return v;
    }
    return eval_term(tok);
}

static val_t eval_mul(const char **pp) {
    val_t v = eval_primary(pp);
    for (;;) {
        expr_skip(pp);
        char op = **pp;
        if (op != '*' && op != '/') break;
        (*pp)++;
        val_t r = eval_primary(pp);
        unsigned ua = (unsigned)v.value & 0xFFFF;
        unsigned ub = (unsigned)r.value & 0xFFFF;
        unsigned prod;
        if (op == '/') {
            if (ub == 0) { errors++; prod = 0; }
            else prod = ua / ub;
        } else {
            prod = (ua * ub) & 0xFFFF;
        }
        if (v.kind == K_ABS && r.kind != K_ABS) v = r;
        v.value = (int)(prod & 0xFFFF);
    }
    return v;
}

static val_t eval_add(const char **pp) {
    val_t base = eval_mul(pp);
    for (;;) {
        expr_skip(pp);
        char op = **pp;
        if (op != '+' && op != '-') break;
        (*pp)++;
        val_t o = eval_mul(pp);
        int sign = op == '+' ? 1 : -1;
        if (o.kind != K_ABS) {
            int addend = (base.kind == K_ABS) ? base.value : 0;
            base = o;
            base.value = (o.value * sign + addend) & 0xFFFF;
        } else {
            base.value = (base.value + sign * o.value) & 0xFFFF;
        }
    }
    return base;
}

static val_t eval_expr(const char *expr) {
    const char *p = expr ? expr : "";
    return eval_add(&p);
}

static void flush_ck(void) {
    if (!ck) return;
    if (ck->ntok > 0 && nchunk < MAXCHUNK) chunks[nchunk++] = *ck;
    ck = NULL;
}

static void begin_ck(void) {
    if (ck || pass != 2) return;
    if (nchunk >= MAXCHUNK) { errors++; return; }
    /* park the open chunk at the end of the array and point at it */
    ck = &chunks[nchunk];
    memset(ck, 0, sizeof(*ck));
    ck->sec = cur;
    ck->off = S()->pc;
    ck->abs = S()->abs;
}

static void add_tok(tok_t t) {
    begin_ck();
    if (ck && ck->ntok >= MAXTOK) {
        flush_ck();
        begin_ck();
    }
    if (!ck || ck->ntok >= MAXTOK) { errors++; return; }
    ck->toks[ck->ntok++] = t;
}

static void list_byte(int b) {
    if (pass != 2 || list_n >= LISTCAP) return;
    list_b[list_n++] = (uint8_t)(b & 0xFF);
}

static void list_mark_word(int mark) {
    if (pass != 2 || !mark || list_n <= 0) return;
    list_mark = mark;
    list_mark_at = list_n - 1;
}

static void lit(int b) {
    ensure_sec();
    if (pass == 2) {
        tok_t t;
        memset(&t, 0, sizeof(t));
        t.k = TK_L;
        t.a = b & 0xFF;
        add_tok(t);
        list_byte(b);
    }
    add_pc(1);
}

static void emit_addr(val_t v) {
    ensure_sec();
    if (v.kind == K_EXT || (v.kind == K_UNDEF && pass == 2)) {
        if (pass == 2) {
            if (ck && ck->ntok) {
                ck->closed_by_ext = 1;
                flush_ck();
            }
            begin_ck();
            tok_t t;
            memset(&t, 0, sizeof(t));
            t.k = TK_EXT;
            t.a = v.value & 0xFFFF;
            strncpy(t.name, v.name, MAXNAME - 1);
            add_tok(t);
            /* The hole bytes are the addend (KEYBUF+LNG_LEN lists 0014x). */
            list_byte((v.value >> 8) & 0xFF);
            list_byte(v.value & 0xFF);
            list_mark_word('x');
        }
        add_pc(2);
        return;
    }
    if (v.kind == K_REL) {
        if (pass == 2) {
            tok_t t;
            memset(&t, 0, sizeof(t));
            if (v.sec == SEC_PSCT) {
                t.k = TK_C2;
                t.a = v.value & 0xFFFF;
            } else {
                /* (0xC0|sec) plus the section offset. Not end-relative. */
                t.k = TK_C1;
                t.a = v.sec;
                t.b = v.value & 0xFFFF;
            }
            add_tok(t);
            list_byte((v.value >> 8) & 0xFF);
            list_byte(v.value & 0xFF);
            list_mark_word('r');
        }
        add_pc(2);
        return;
    }
    lit((v.value >> 8) & 0xFF);
    lit(v.value & 0xFF);
}

/* Direct-page reloc: object marker (0x20|sec) plus the 16-bit offset,
 * one byte in the instruction. The linker stores the low 8 bits. */
static void emit_d8(val_t v) {
    ensure_sec();
    if (pass == 2) {
        tok_t t;
        memset(&t, 0, sizeof(t));
        t.k = TK_D8;
        t.a = v.sec & 0x0F;
        t.b = v.value & 0xFFFF;
        add_tok(t);
        list_byte(v.value & 0xFF);
        list_mark_word('r');
    }
    add_pc(1);
}

/* BSCT is page 0 unless SETDP has moved the direct page. */
static int rel_is_direct(const val_t *v) {
    if (v->kind != K_REL || v->sec != SEC_BSCT) return 0;
    return (direct_page & 0xFF) == 0;
}

static void add_meta(const meta_t *m) {
    if (pass != 2) return;
    if (nmeta >= MAXMETA) { errors++; fprintf(stderr, "section table full\n"); return; }
    metas[nmeta++] = *m;
}

/* One S4 per contiguous span. Relative re-entry continues the PC. */
static void close_span(int sec) {
    if (pass != 2 || sec < 0 || sec >= MAXSEC) return;
    sec_t *s = &secs[sec];
    if (!s->span_open) return;
    s->span_open = 0;
    int start = s->span_start & 0xFFFF;
    int pc = s->pc & 0xFFFF;
    int size = (pc - start) & 0xFFFF;
    if (size == 0) return;
    meta_t m;
    memset(&m, 0, sizeof(m));
    m.sec = sec;
    m.end = (pc - 1) & 0xFFFF;
    if (s->abs) {
        m.kind = 1;
        m.org = start;
    } else if (start == 0) {
        m.kind = 0;
    } else {
        m.kind = 3;
        m.org = start;
    }
    add_meta(&m);
}

static void open_span(int sec) {
    if (sec < 0 || sec >= MAXSEC) return;
    secs[sec].span_start = secs[sec].pc;
    secs[sec].span_open = 1;
    secs[sec].used = 1;
}

static void announce_named(int sec) {
    if (pass != 2 || sec < SEC_BASE || secs[sec].announced) return;
    if (!secs[sec].name[0]) return;
    secs[sec].announced = 1;
    meta_t m;
    memset(&m, 0, sizeof(m));
    m.kind = 4;
    m.sec = sec;
    strncpy(m.name, secs[sec].name, MAXNAME - 1);
    add_meta(&m);
}

static int find_named(const char *name) {
    for (int i = SEC_BASE; i < nsec; i++)
        if (ieq(secs[i].name, name)) return i;
    return -1;
}

static int ensure_named(const char *name) {
    int id = find_named(name);
    if (id >= 0) return id;
    if (nsec >= MAXSEC) {
        fprintf(stderr, "user definable sections exhausted\n");
        errors++;
        return SEC_DSCT;
    }
    id = nsec++;
    memset(&secs[id], 0, sizeof(secs[id]));
    secs[id].org = -1;
    strncpy(secs[id].name, name, MAXNAME - 1);
    return id;
}

static void switch_sec(int sec) {
    if (sec < 0 || sec >= MAXSEC) return;
    if (sec >= SEC_BASE) announce_named(sec);
    flush_ck();
    /* A second PSCT (or DSCT) while that section is already open
     * continues the same span. Resetting the start here dropped the
     * first span and made the S4 look like a continuation. */
    if (cur == sec) {
        if (!secs[sec].span_open) open_span(sec);
        return;
    }
    if (cur >= 0) close_span(cur);
    cur = sec;
    open_span(cur);
}

/* An absolute ORG or a bare instruction opens PSCT. Walking secs[-1]
 * is what used to abort the larger jobs. */
static void ensure_sec(void) {
    if (cur < 0) switch_sec(SEC_PSCT);
}

static int sec_of(const char *n) {
    if (ieq(n, "ASCT")) return SEC_ASCT;
    if (ieq(n, "DSCT")) return SEC_DSCT;
    if (ieq(n, "PSCT")) return SEC_PSCT;
    if (ieq(n, "BSCT")) return SEC_BSCT;
    return -1;
}

static const opc_t *find_op(const char *m) {
    for (int i = 0; i < NOPS; i++) if (ieq(OPS[i].mnem, m)) return &OPS[i];
    return NULL;
}

static int reg_id(const char *r) {
    if (ieq(r, "X")) return 0;
    if (ieq(r, "Y")) return 1;
    if (ieq(r, "U")) return 2;
    if (ieq(r, "S")) return 3;
    return -1;
}

/* Indexed postbyte. *reloc is set when a 16-bit reloc follows the postbyte. */
static int encode_index(const char *operand, uint8_t *bytes, val_t *reloc, int *has_rel) {
    char buf[128];
    strncpy(buf, operand, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *op = buf;
    int indirect = 0;
    size_t L = strlen(op);
    if (L >= 2 && op[0] == '[' && op[L - 1] == ']') {
        indirect = 1;
        op[L - 1] = 0;
        op++;
    }
    char *comma = strrchr(op, ',');
    /* WINTEK [addr] has no comma. It is indirect extended: postbyte $9F
     * and a 16-bit address (STD [PBADR] lists ED 9F addr). */
    if (!comma) {
        *has_rel = 0;
        if (!indirect) { errors++; bytes[0] = 0x84; return 1; }
        char *left = op;
        while (*left && isspace((unsigned char)*left)) left++;
        char *end = left + strlen(left);
        while (end > left && isspace((unsigned char)end[-1])) *--end = 0;
        val_t v = eval_expr(left);
        bytes[0] = 0x9F;
        if (v.kind != K_ABS) {
            *reloc = v;
            *has_rel = 1;
            return 1;
        }
        bytes[1] = (uint8_t)((v.value >> 8) & 0xFF);
        bytes[2] = (uint8_t)(v.value & 0xFF);
        return 3;
    }
    *comma = 0;
    char *regn = comma + 1;
    while (*regn && isspace((unsigned char)*regn)) regn++;
    int auto_m = -1;
    size_t rl = strlen(regn);
    if (rl >= 2 && regn[rl - 2] == '+' && regn[rl - 1] == '+') { auto_m = 1; regn[rl - 2] = 0; }
    else if (rl >= 1 && regn[rl - 1] == '+') { auto_m = 0; regn[rl - 1] = 0; }
    else if (regn[0] == '-' && regn[1] == '-') { auto_m = 3; regn += 2; }
    else if (regn[0] == '-') { auto_m = 2; regn += 1; }
    int reg = reg_id(regn);
    if (reg < 0) { errors++; bytes[0] = 0x84; return 1; }
    *has_rel = 0;
    if (auto_m >= 0) {
        int post = 0x80 | (reg << 5) | auto_m;
        if (indirect) post |= 0x10;
        bytes[0] = (uint8_t)post;
        return 1;
    }
    char *left = op;
    while (*left && isspace((unsigned char)*left)) left++;
    /* Trailing spaces on the offset must not hide A/B/D. */
    {
        char *end = left + strlen(left);
        while (end > left && isspace((unsigned char)end[-1])) *--end = 0;
    }
    if (ieq(left, "A") || ieq(left, "B") || ieq(left, "D") ||
        ieq(left, "E") || ieq(left, "F") || ieq(left, "W")) {
        int mode = ieq(left, "B") ? 0x05 : ieq(left, "A") ? 0x06 :
                   ieq(left, "E") ? 0x07 : ieq(left, "F") ? 0x0A :
                   ieq(left, "W") ? 0x0E : 0x0B;
        int post = 0x80 | (reg << 5) | mode;
        if (indirect) post |= 0x10;
        bytes[0] = (uint8_t)post;
        return 1;
    }
    if (*left == 0) {
        int post = 0x84 | (reg << 5);
        if (indirect) post |= 0x10;
        bytes[0] = (uint8_t)post;
        return 1;
    }
    val_t v = eval_expr(left);
    if (v.kind != K_ABS) {
        int post = 0x89 | (reg << 5);
        if (indirect) post |= 0x10;
        bytes[0] = (uint8_t)post;
        *reloc = v;
        *has_rel = 1;
        return 1;
    }
    int off = v.value;
    if (off & 0x8000) off -= 0x10000;
    if (off == 0 && !indirect) {
        bytes[0] = (uint8_t)(0x84 | (reg << 5));
        return 1;
    }
    if (off >= -16 && off <= 15 && off != 0 && !indirect) {
        bytes[0] = (uint8_t)((reg << 5) | (off & 0x1F));
        return 1;
    }
    if (off >= -128 && off <= 127) {
        int post = 0x88 | (reg << 5);
        if (indirect) post |= 0x10;
        bytes[0] = (uint8_t)post;
        bytes[1] = (uint8_t)(off & 0xFF);
        return 2;
    }
    int post = 0x89 | (reg << 5);
    if (indirect) post |= 0x10;
    bytes[0] = (uint8_t)post;
    bytes[1] = (uint8_t)((v.value >> 8) & 0xFF);
    bytes[2] = (uint8_t)(v.value & 0xFF);
    return 3;
}

static int stack_post(const char *operand) {
    int post = 0;
    char buf[128];
    strncpy(buf, operand, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *save = NULL;
    for (char *p = strtok_r(buf, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (ieq(p, "CC") || ieq(p, "C")) post |= 0x01;
        else if (ieq(p, "A")) post |= 0x02;
        else if (ieq(p, "B")) post |= 0x04;
        else if (ieq(p, "D")) post |= 0x06;
        else if (ieq(p, "DP")) post |= 0x08;
        else if (ieq(p, "X")) post |= 0x10;
        else if (ieq(p, "Y")) post |= 0x20;
        else if (ieq(p, "U") || ieq(p, "S")) post |= 0x40;
        else if (ieq(p, "PC")) post |= 0x80;
    }
    return post;
}

static int is_imm16(const char *m) {
    return ieq(m, "LDD") || ieq(m, "LDX") || ieq(m, "LDU") || ieq(m, "LDS") ||
           ieq(m, "LDY") || ieq(m, "SUBD") || ieq(m, "ADDD") || ieq(m, "CMPD") ||
           ieq(m, "CMPX") || ieq(m, "CPX") || ieq(m, "CMPY") || ieq(m, "CMPU") ||
           ieq(m, "CMPS");
}

static int tfr_reg(const char *r) {
    while (*r && isspace((unsigned char)*r)) r++;
    char buf[8];
    int i = 0;
    while (*r && !isspace((unsigned char)*r) && *r != ',' && i < 7) buf[i++] = *r++;
    buf[i] = 0;
    if (ieq(buf, "D")) return 0;
    if (ieq(buf, "X")) return 1;
    if (ieq(buf, "Y")) return 2;
    if (ieq(buf, "U")) return 3;
    if (ieq(buf, "S")) return 4;
    if (ieq(buf, "PC")) return 5;
    if (ieq(buf, "A")) return 8;
    if (ieq(buf, "B")) return 9;
    if (ieq(buf, "CC")) return 10;
    if (ieq(buf, "DP")) return 11;
    if (ieq(buf, "W")) return 6;
    if (ieq(buf, "V")) return 7;
    if (ieq(buf, "E")) return 14;
    if (ieq(buf, "F")) return 15;
    if (ieq(buf, "0")) return 12;
    return -1;
}

/* `S+`, `--X`, and a bare `X` are indexed forms with the comma omitted. */
static int bare_index(const char *op, char *out, size_t n) {
    const char *s = op;
    const char *pre = "";
    if (s[0] == '-' && s[1] == '-') { pre = "--"; s += 2; }
    else if (s[0] == '-') { pre = "-"; s += 1; }
    if (!*s) return 0;
    char reg = (char)toupper((unsigned char)s[0]);
    if (reg != 'X' && reg != 'Y' && reg != 'U' && reg != 'S') return 0;
    s++;
    const char *post = "";
    if (s[0] == '+' && s[1] == '+') { post = "++"; s += 2; }
    else if (s[0] == '+') { post = "+"; s += 1; }
    if (*s) return 0;
    snprintf(out, n, ",%s%c%s", pre, reg, post);
    return 1;
}

static const char *skip_ws(const char *s) {
    while (s && *s && isspace((unsigned char)*s)) s++;
    return s ? s : "";
}

static void bad_operand(const char *mnem, const char *operand) {
    fprintf(stderr, "bad operand: %s %s\n", mnem, operand ? operand : "");
    errors++;
}

/* 1 = direct byte, 2 = extended word, 3 = direct reloc (d8).
 * Same choice the 6809 memory tail makes, including a forced < or >. */
static int ea_abs_mode(const opc_t *op, const char *operand, val_t *v) {
    const char *expr = operand ? operand : "";
    int force = 0;
    if (expr[0] == '<') { force = 1; expr++; }
    else if (expr[0] == '>') { force = 2; expr++; }
    *v = expr[0] ? eval_expr(expr) : (val_t){0, K_ABS, -1, {0}};
    int mode = 2;
    if (force) mode = force;
    else if (op->od >= 0 && rel_is_direct(v)) mode = 1;
    else if (v->kind == K_REL || v->kind == K_EXT || v->kind == K_UNDEF) mode = 2;
    else if (v->kind == K_ABS && op->od >= 0 &&
             ((v->value >> 8) & 0xFF) == (direct_page & 0xFF)) mode = 1;
    if (mode == 1 && v->kind == K_REL && (v->sec & 0x0F) != 0) return 3;
    return mode;
}

static void emit_abs_ea(int mode, val_t v) {
    if (mode == 3) emit_d8(v);
    else if (mode == 1) lit(v.value & 0xFF);
    else emit_addr(v);
}

/* A single $ / 0x / % / decimal token keeps 32 bits. Expressions stay
 * on the 16-bit evaluator so indexed negatives do not change. */
static int imm32_token(const char *s, unsigned long *out) {
    const char *p = skip_ws(s);
    int neg = 0;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }
    const char *digs = p;
    int base = 10;
    if (p[0] == '$') { base = 16; digs = p + 1; }
    else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; digs = p + 2; }
    else if (p[0] == '%') { base = 2; digs = p + 1; }
    else if (!isdigit((unsigned char)p[0])) return 0;
    if (!*digs || isspace((unsigned char)*digs)) return 0;
    char *end = NULL;
    unsigned long v = strtoul(digs, &end, base);
    if (!end || end == digs || *skip_ws(end) != 0) return 0;
    *out = neg ? (0UL - v) : v;
    return 1;
}

static unsigned long imm32_value(const char *expr) {
    unsigned long v = 0;
    if (imm32_token(expr, &v)) return v;
    val_t ev = eval_expr(expr);
    return (unsigned long)(ev.value & 0xFFFF);
}

/* LDQ immediate is CD and four bytes. The page prefix belongs to the
 * memory forms only. */
static void do_ldq_imm(const opc_t *op, const char *expr) {
    if (op->oi < 0) {
        fprintf(stderr, "illegal immediate\n");
        errors++;
        return;
    }
    unsigned long v = imm32_value(expr);
    lit(op->oi);
    lit((int)((v >> 24) & 0xFF));
    lit((int)((v >> 16) & 0xFF));
    lit((int)((v >> 8) & 0xFF));
    lit((int)(v & 0xFF));
}

/* OIM/AIM/EIM/TIM are opcode, then the immediate, then the address. */
static void do_immea(const opc_t *op, const char *mnem, const char *operand) {
    char left[128];
    char idxbuf[16];
    const char *comma;
    const char *addr;
    const char *mem;
    size_t n;
    val_t iv;
    int imm;
    if (!operand || operand[0] != '#') {
        bad_operand(mnem, operand);
        return;
    }
    comma = strchr(operand, ',');
    if (!comma) {
        bad_operand(mnem, operand);
        return;
    }
    n = (size_t)(comma - operand);
    if (n >= sizeof(left)) n = sizeof(left) - 1;
    memcpy(left, operand, n);
    left[n] = 0;
    addr = skip_ws(comma + 1);
    if (!addr[0]) {
        bad_operand(mnem, operand);
        return;
    }
    iv = eval_expr(left + 1);
    imm = iv.value & 0xFF;
    mem = addr;
    if (bare_index(addr, idxbuf, sizeof(idxbuf))) mem = idxbuf;
    if (strchr(mem, ',') || mem[0] == '[') {
        uint8_t b[4];
        val_t rel;
        int has = 0;
        int nb;
        if (op->ox < 0) {
            bad_operand(mnem, operand);
            return;
        }
        nb = encode_index(mem, b, &rel, &has);
        if (op->page) lit(op->page);
        lit(op->ox);
        lit(imm);
        for (int i = 0; i < nb; i++) lit(b[i]);
        if (has) emit_addr(rel);
        return;
    }
    {
        val_t v;
        int mode = ea_abs_mode(op, addr, &v);
        int opc = (mode == 2) ? op->oe : op->od;
        if (opc < 0) {
            bad_operand(mnem, operand);
            return;
        }
        if (op->page) lit(op->page);
        lit(opc);
        lit(imm);
        emit_abs_ea(mode, v);
    }
}

static int bit_field_at(const char *s, int *field, const char **end) {
    char buf[8];
    int i = 0;
    s = skip_ws(s);
    while (*s && !isspace((unsigned char)*s) && *s != ',' && i < 7) buf[i++] = *s++;
    buf[i] = 0;
    *end = s;
    if (ieq(buf, "CC") || ieq(buf, "C")) { *field = 0; return 1; }
    if (ieq(buf, "A")) { *field = 1; return 1; }
    if (ieq(buf, "B")) { *field = 2; return 1; }
    return 0;
}

static int take_bit(const char *s, int *bit, const char **end) {
    char *e = NULL;
    unsigned long v;
    s = skip_ws(s);
    if (*s == '$') {
        v = strtoul(s + 1, &e, 16);
        if (e == s + 1) return 0;
    } else if (*s == '%') {
        v = strtoul(s + 1, &e, 2);
        if (e == s + 1) return 0;
    } else if (isdigit((unsigned char)*s)) {
        v = strtoul(s, &e, 10);
        if (e == s) return 0;
    } else {
        return 0;
    }
    if (v > 7) return 0;
    *bit = (int)v;
    *end = e;
    return 1;
}

/* BAND..STBT are direct only. An indexed or extended address emits nothing. */
static void do_bit(const opc_t *op, const char *mnem, const char *operand) {
    const char *end = NULL;
    const char *addr;
    char idxbuf[16];
    const char *mem;
    opc_t direct;
    val_t v;
    int field = 0, sbit = 0, dbit = 0, mode;
    if (!bit_field_at(operand ? operand : "", &field, &end)) {
        bad_operand(mnem, operand);
        return;
    }
    end = skip_ws(end);
    if (*end != ',' || !take_bit(end + 1, &sbit, &end)) {
        bad_operand(mnem, operand);
        return;
    }
    end = skip_ws(end);
    if (*end != ',' || !take_bit(end + 1, &dbit, &end)) {
        bad_operand(mnem, operand);
        return;
    }
    end = skip_ws(end);
    if (*end != ',') {
        bad_operand(mnem, operand);
        return;
    }
    addr = skip_ws(end + 1);
    if (!addr[0]) {
        bad_operand(mnem, operand);
        return;
    }
    mem = addr;
    if (bare_index(addr, idxbuf, sizeof(idxbuf))) mem = idxbuf;
    if (strchr(mem, ',') || mem[0] == '[') {
        bad_operand(mnem, operand);
        return;
    }
    direct = *op;
    direct.od = 0;
    direct.oe = -1;
    direct.ox = -1;
    mode = ea_abs_mode(&direct, addr, &v);
    if (mode == 2) {
        bad_operand(mnem, operand);
        return;
    }
    if (op->page) lit(op->page);
    lit(op->oi);
    lit((field << 6) | ((sbit & 7) << 3) | (dbit & 7));
    emit_abs_ea(mode, v);
}

/* TFM registers are D, X, Y, U, and S. ++ and -- are indexed, not TFM. */
static int tfm_side(const char *s, int *reg, int *sign) {
    char buf[8];
    int i = 0;
    s = skip_ws(s);
    while (*s && !isspace((unsigned char)*s) && *s != '+' && *s != '-' && *s != ',' && i < 7)
        buf[i++] = *s++;
    buf[i] = 0;
    if (!buf[0]) return 0;
    *reg = tfr_reg(buf);
    if (*reg < 0 || *reg > 4) return 0;
    s = skip_ws(s);
    if ((s[0] == '+' && s[1] == '+') || (s[0] == '-' && s[1] == '-')) return 0;
    *sign = 0;
    if (*s == '+') { *sign = 1; s++; }
    else if (*s == '-') { *sign = -1; s++; }
    return *skip_ws(s) == 0;
}

static int tfm_opc(int ss, int ds) {
    if (ss == 1 && ds == 1) return 0x38;
    if (ss == -1 && ds == -1) return 0x39;
    if (ss == 1 && ds == 0) return 0x3A;
    if (ss == 0 && ds == 1) return 0x3B;
    return -1;
}

static void do_tfm(const opc_t *op, const char *mnem, const char *operand) {
    char buf[128];
    char *comma;
    int sr = 0, dr = 0, ss = 0, ds = 0, opc;
    strncpy(buf, operand ? operand : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    comma = strchr(buf, ',');
    if (!comma) {
        bad_operand(mnem, operand);
        return;
    }
    *comma = 0;
    if (!tfm_side(buf, &sr, &ss) || !tfm_side(comma + 1, &dr, &ds)) {
        bad_operand(mnem, operand);
        return;
    }
    opc = tfm_opc(ss, ds);
    if (opc < 0) {
        bad_operand(mnem, operand);
        return;
    }
    if (op->page) lit(op->page);
    lit(opc);
    lit((sr << 4) | dr);
}

static void do_instr(const opc_t *op, const char *mnem, const char *operand) {
    if (!operand) operand = "";
    if (op->kind == K_INH) {
        if (op->page) lit(op->page);
        lit(op->oi);
        return;
    }
    if (op->kind == K_IMM8) {
        if (op->page) lit(op->page);
        lit(op->oi);
        const char *ex = (operand[0] == '#') ? operand + 1 : operand;
        val_t v = ex[0] ? eval_expr(ex) : (val_t){0, K_ABS, -1, {0}};
        lit(v.value & 0xFF);
        return;
    }
    if (op->kind == K_TFR) {
        if (op->page) lit(op->page);
        lit(op->oi);
        char buf[64];
        strncpy(buf, operand, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char *comma = strchr(buf, ',');
        int src = -1, dst = -1;
        if (comma) { *comma = 0; src = tfr_reg(buf); dst = tfr_reg(comma + 1); }
        if (src < 0 || dst < 0) {
            fprintf(stderr, "bad register: %s %s\n", mnem, operand);
            errors++;
            src = 0;
            dst = 0;
        }
        lit((src << 4) | dst);
        return;
    }
    if (op->kind == K_LREL) {
        if (op->page) lit(op->page);
        lit(op->oi);
        if (pass == 1 || !operand[0]) {
            lit(0);
            lit(0);
        } else {
            val_t v = eval_expr(operand);
            int pc = S()->pc;
            int off = (v.value - ((pc + 2) & 0xFFFF)) & 0xFFFF;
            lit((off >> 8) & 0xFF);
            lit(off & 0xFF);
        }
        return;
    }
    if (op->kind == K_RELB) {
        lit(op->oi);
        if (pass == 1 || !operand || !operand[0]) {
            lit(0);
        } else {
            val_t v = eval_expr(operand);
            int dest = v.value;
            int off = (dest - ((S()->pc + 1) & 0xFFFF)) & 0xFFFF;
            int soff = (off & 0x8000) ? off - 0x10000 : off;
            if (soff < -128 || soff > 127) {
                fprintf(stderr, "branch out of range: %s %s\n", mnem, operand);
                errors++;
            }
            lit(off & 0xFF);
        }
        return;
    }
    if (op->kind == K_STK) {
        lit(op->oi);
        lit(stack_post(operand));
        return;
    }
    if (op->kind == K_LEA) {
        lit(op->oi);
        uint8_t b[4];
        val_t rel;
        int has = 0;
        int n = encode_index(operand, b, &rel, &has);
        for (int i = 0; i < n; i++) lit(b[i]);
        if (has) emit_addr(rel);
        return;
    }
    if (op->kind == K_IMM32 && operand[0] == '#') {
        do_ldq_imm(op, operand + 1);
        return;
    }
    if (op->kind == K_IMMEA) {
        do_immea(op, mnem, operand);
        return;
    }
    if (op->kind == K_BIT) {
        do_bit(op, mnem, operand);
        return;
    }
    if (op->kind == K_TFM) {
        do_tfm(op, mnem, operand);
        return;
    }
    if (!operand[0]) {
        fprintf(stderr, "operand required: %s\n", mnem);
        errors++;
        return;
    }
    if (operand[0] == '#') {
        if (op->oi < 0) {
            fprintf(stderr, "illegal immediate: %s\n", mnem);
            errors++;
            return;
        }
        if (op->page) lit(op->page);
        lit(op->oi);
        val_t v = eval_expr(operand + 1);
        if (op->kind == K_IMM16 || is_imm16(mnem)) emit_addr(v);
        else lit(v.value & 0xFF);
        return;
    }
    /* `JSR X` and `CMPA S+` omit the comma. XMSGSET stays a symbol. */
    char idxbuf[16];
    const char *mem = operand;
    if (bare_index(operand, idxbuf, sizeof(idxbuf))) mem = idxbuf;
    if (strchr(mem, ',') || mem[0] == '[') {
        if (op->page) lit(op->page);
        lit(op->ox);
        uint8_t b[4];
        val_t rel;
        int has = 0;
        int n = encode_index(mem, b, &rel, &has);
        for (int i = 0; i < n; i++) lit(b[i]);
        if (has) emit_addr(rel);
        return;
    }
    const char *expr = operand;
    int force = 0; /* 1 dir, 2 ext */
    if (expr[0] == '<') { force = 1; expr++; }
    else if (expr[0] == '>') { force = 2; expr++; }
    val_t v = expr[0] ? eval_expr(expr) : (val_t){0, K_ABS, -1, {0}};
    int mode = 2;
    if (force) mode = force;
    else if (op->od >= 0 && rel_is_direct(&v)) mode = 1;
    else if (v.kind == K_REL || v.kind == K_EXT || v.kind == K_UNDEF) mode = 2;
    else if (v.kind == K_ABS && op->od >= 0 &&
             ((v.value >> 8) & 0xFF) == (direct_page & 0xFF)) mode = 1;
    if (mode == 1 && v.kind == K_REL && (v.sec & 0x0F) != 0) {
        if (op->page) lit(op->page);
        lit(op->od);
        emit_d8(v);
    } else if (mode == 1) {
        if (op->page) lit(op->page);
        lit(op->od);
        lit(v.value & 0xFF);
    } else {
        if (op->page) lit(op->page);
        lit(op->oe);
        emit_addr(v);
    }
}

static void note_label(const char *label) {
    if (!label || cur < 0) return;
    if (S()->abs) define_sym(label, S()->pc, K_ABS, -1);
    else define_sym(label, S()->pc, K_REL, cur);
    if (pass == 2 && has_name(globals, nglob, label)) {
        if (nmeta >= MAXMETA) { errors++; return; }
        meta_t *m = &metas[nmeta++];
        memset(m, 0, sizeof(*m));
        m->kind = 2;
        m->sec = cur;
        m->off = S()->pc;
        strncpy(m->name, label, MAXNAME - 1);
    }
}

static void split_names(const char *operand, char arr[][MAXNAME], int *n) {
    char buf[256];
    strncpy(buf, operand, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *save = NULL;
    for (char *p = strtok_r(buf, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
        while (*p && isspace((unsigned char)*p)) p++;
        char *e = p + strlen(p);
        while (e > p && isspace((unsigned char)e[-1])) *--e = 0;
        if (*p) add_name(arr, n, p);
    }
}

static int assembling(void) {
    if (ifsp <= 0) return 1;
    return ifs[ifsp - 1].parent_on && ifs[ifsp - 1].taken;
}

static int kind_of(val_t v, int *sec) {
    if (v.kind == K_REL) { *sec = v.sec; return K_REL; }
    *sec = -1;
    return K_ABS;
}

/* mode 0 = IFEQ, 1 = IFNE, 2 = IFGE, 3 = IFLE, 4 = IFGT, 5 = IFLT. */
static void push_if(int mode, const char *operand) {
    int parent = assembling();
    int take = 0;
    if (parent) {
        val_t v = operand && operand[0] ? eval_expr(operand) : (val_t){0, K_ABS, -1, {0}};
        int s = v.value & 0xFFFF;
        if (s & 0x8000) s -= 0x10000;
        if (mode == 1) take = (v.value != 0);
        else if (mode == 2) take = (s >= 0);
        else if (mode == 3) take = (s <= 0);
        else if (mode == 4) take = (s > 0);
        else if (mode == 5) take = (s < 0);
        else take = (v.value == 0);
    }
    if (ifsp >= 32) { errors++; return; }
    ifs[ifsp].parent_on = parent;
    ifs[ifsp].taken = take;
    ifs[ifsp].saw_else = 0;
    ifsp++;
}

/* IFC / IFNC compare the two comma fields as text (`IFC \9,2`). */
static int ifc_equal(const char *operand) {
    char a[80], b[80];
    const char *s = operand ? operand : "";
    while (*s && isspace((unsigned char)*s)) s++;
    const char *c = strchr(s, ',');
    size_t na = c ? (size_t)(c - s) : strlen(s);
    if (na >= sizeof(a)) na = sizeof(a) - 1;
    memcpy(a, s, na);
    a[na] = 0;
    while (na && isspace((unsigned char)a[na - 1])) a[--na] = 0;
    b[0] = 0;
    if (c) {
        c++;
        while (*c && isspace((unsigned char)*c)) c++;
        strncpy(b, c, sizeof(b) - 1);
        b[sizeof(b) - 1] = 0;
        size_t nb = strlen(b);
        while (nb && isspace((unsigned char)b[nb - 1])) b[--nb] = 0;
    }
    return strcmp(a, b) == 0;
}

static void push_ifc(int want_equal, const char *operand) {
    int parent = assembling();
    int take = 0;
    if (parent) take = want_equal ? ifc_equal(operand) : !ifc_equal(operand);
    if (ifsp >= 32) { errors++; return; }
    ifs[ifsp].parent_on = parent;
    ifs[ifsp].taken = take;
    ifs[ifsp].saw_else = 0;
    ifsp++;
}

static int find_macro(const char *name) {
    for (int i = 0; i < nmac; i++) if (ieq(macros[i].name, name)) return i;
    return -1;
}

static int keep_full(const char *mnem) {
    return ieq(mnem, "GLOBAL") || ieq(mnem, "EXTERN") || ieq(mnem, "FCC") ||
           ieq(mnem, "TTL") || ieq(mnem, "STL") || ieq(mnem, "SPC") ||
           ieq(mnem, "FCB") || ieq(mnem, "FDB");
}

static int is_macr(const char *mnem) {
    return ieq(mnem, "MACR") || ieq(mnem, "MACRO");
}

/* Column 0 is normally a label. PAGE, ORG, END, and the other
 * directives are also written there (`PAGE 001`). */
static int is_directive(const char *m) {
    static const char *d[] = {
        "PAGE", "ORG", "END", "ENDM", "OPT", "NAM", "TTL", "STL", "SPC",
        "PSCT", "DSCT", "ASCT", "BSCT", "SETDP", "GLOBAL", "EXTERN",
        "XDEF", "XREF", "MACR", "MACRO", "IFEQ", "IFNE", "IFGE", "IFLE",
        "IFGT", "IFLT", "IFC", "IFNC",
        "ELSE", "ENDC", "INCLUDE", "INCL", "FCC", "FCB", "FDB", "RMB",
        "BSZ", "EQU", "SET", "LIST", "NOLIST", "NOL", NULL
    };
    for (int i = 0; d[i]; i++) if (ieq(m, d[i])) return 1;
    return 0;
}

/* Directives that must not open a section by themselves. */
static int emits_code(const char *m) {
    static const char *skip[] = {
        "EQU", "SET", "NAM", "TTL", "STL", "SPC", "GLOBAL", "EXTERN",
        "END", "OPT", "PAGE", "SETDP", "IFEQ", "IFNE", "IFGE", "IFLE",
        "IFGT", "IFLT", "IFC", "IFNC",
        "ELSE", "ENDC", "MACR", "MACRO", "ENDM",
        "LIST", "NOLIST", "NOL", NULL
    };
    for (int i = 0; skip[i]; i++) if (ieq(m, skip[i])) return 0;
    return 1;
}

/* First word of each comma field. A * or ; field ends the list.
 * A space that is not the gap before the next comma ends the list too:
 * `FDB IN4,IN4BUF,HORN2  CH 25-27,31` must not assemble 31. */
static int next_item(char **ps, char *tok, size_t ntok) {
    char *s = *ps;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s || *s == '*' || *s == ';') { *ps = s; return 0; }
    size_t i = 0;
    while (*s && *s != ',' && !isspace((unsigned char)*s) && i + 1 < ntok) tok[i++] = *s++;
    tok[i] = 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == ',') s++;
    else {
        while (*s) s++;
    }
    *ps = s;
    return tok[0] != 0;
}

/* 6800 spellings assemble to 6809 machine code so a 6800 source
 * ports more easily. LDAA and LDA are the same instruction, as are
 * LDAB/LDB, STAA/STA, STAB/STB, ORAA/ORA, and ORAB/ORB. The other
 * 6800 names in this list also emit 6809 code. A pure 6800 job
 * needs 6800 opcodes, in a 6800 mode or a separate assembler. */
static void apply_alias(char *mnem, char *operand) {
    struct { const char *from, *to, *imm; } al[] = {
        {"CLC", "ANDCC", "#$FE"}, {"SEC", "ORCC", "#$01"},
        {"CLI", "ANDCC", "#$EF"}, {"SEI", "ORCC", "#$10"},
        {"CLV", "ANDCC", "#$FD"}, {"SEV", "ORCC", "#$02"},
        {"CPX", "CMPX", NULL}, {"PUSHU", "PSHU", NULL}, {"PUSHS", "PSHS", NULL},
        {"POPU", "PULU", NULL}, {"POPS", "PULS", NULL}, {"CALL", "JSR", NULL},
        {"ORAA", "ORA", NULL}, {"ORAB", "ORB", NULL},
        {"LDAA", "LDA", NULL}, {"LDAB", "LDB", NULL},
        {"STAA", "STA", NULL}, {"STAB", "STB", NULL},
        {"LSLA", "ASLA", NULL}, {"LSLB", "ASLB", NULL},
        {"ASLD", "LSLD", NULL},
        {"PSHA", "PSHS", "A"}, {"PSHB", "PSHS", "B"},
        {"PULA", "PULS", "A"}, {"PULB", "PULS", "B"},
        {"SEIF", "ORCC", "#$50"},
        {"INX", "LEAX", "1,X"}, {"DEX", "LEAX", "-1,X"},
        /* INY is not a 6809 opcode. Job 2196 lists it as `31 21`
         * (LEAY 1,Y). A source macro of the same name still wins,
         * which is how 1475 simulates Y in RAM. */
        {"INY", "LEAY", "1,Y"}, {"DEY", "LEAY", "-1,Y"},
        {"INS", "LEAS", "1,S"}, {"DES", "LEAS", "-1,S"},
        {"TSX", "TFR", "S,X"}, {"TXS", "TFR", "X,S"},
        {"TPA", "TFR", "CC,A"}, {"TAP", "TFR", "A,CC"},
        {"XDEF", "GLOBAL", NULL}, {"XREF", "EXTERN", NULL},
    };
    for (size_t i = 0; i < sizeof(al) / sizeof(al[0]); i++) {
        if (!ieq(mnem, al[i].from)) continue;
        strncpy(mnem, al[i].to, 31);
        mnem[31] = 0;
        if (al[i].imm) {
            strncpy(operand, al[i].imm, 511);
            operand[511] = 0;
        }
        return;
    }
    /* 6800 `PSH B` / `PUL A` carry the register in the operand. */
    if (ieq(mnem, "PSH") || ieq(mnem, "PUL")) {
        char *s = operand;
        while (*s && isspace((unsigned char)*s)) s++;
        char r = (char)toupper((unsigned char)s[0]);
        if ((r == 'A' || r == 'B') && (s[1] == 0 || isspace((unsigned char)s[1]))) {
            strncpy(mnem, ieq(mnem, "PSH") ? "PSHS" : "PULS", 31);
            mnem[31] = 0;
            operand[0] = r;
            operand[1] = 0;
        }
    }
}

/* Returns 0 when END stops the file. */
static int assemble_line(char *line) {
    char *raw = line;
    size_t n = strlen(raw);
    while (n && (raw[n - 1] == '\n' || raw[n - 1] == '\r')) raw[--n] = 0;
    char *z = strchr(raw, '\x1a');
    if (z) *z = 0;
    /* A comment may be indented. Column 0 is still what makes a label. */
    {
        const char *scan = raw;
        while (*scan && isspace((unsigned char)*scan)) scan++;
        if (!*scan || *scan == '*' || *scan == ';') { list_plain = 1; return 1; }
    }
    snap_star();

    char *label = NULL;
    char *rest = raw;
    if (raw[0] != ' ' && raw[0] != '\t') {
        char word[32];
        size_t wi = 0;
        while (raw[wi] && !isspace((unsigned char)raw[wi]) && wi + 1 < sizeof(word)) {
            word[wi] = raw[wi];
            wi++;
        }
        word[wi] = 0;
        if (!is_directive(word)) {
            label = raw;
            rest = raw + wi;
            if (*rest) { *rest = 0; rest++; }
            else rest = raw + strlen(raw);
        }
    }
    while (*rest && isspace((unsigned char)*rest)) rest++;
    if (!*rest) {
        if (assembling()) note_label(label);
        else list_hide = 1;
        return 1;
    }
    char mnem[32];
    {
        size_t i = 0;
        while (*rest && !isspace((unsigned char)*rest) && i + 1 < sizeof(mnem)) mnem[i++] = *rest++;
        mnem[i] = 0;
        if (*rest && !isspace((unsigned char)*rest))
            while (*rest && !isspace((unsigned char)*rest)) rest++;
    }
    while (*rest && isspace((unsigned char)*rest)) rest++;

    char operand[512];
    operand[0] = 0;
    if (*rest) {
        strncpy(operand, rest, sizeof(operand) - 1);
        operand[sizeof(operand) - 1] = 0;
    }
    /* PUSHU and CALL are macros in the real-time sources. The 6800
     * alias applies only when no macro of that name is defined. */
    if (find_macro(mnem) < 0)
        apply_alias(mnem, operand);
    /* `ADD A #48` / `SUB B #10` are the 6800 accumulator form of ADDA/SUBB. */
    if (ieq(mnem, "ADD") || ieq(mnem, "SUB") || ieq(mnem, "ADC") || ieq(mnem, "SBC")) {
        char *s = operand;
        while (*s && isspace((unsigned char)*s)) s++;
        char r = (char)toupper((unsigned char)s[0]);
        if ((r == 'A' || r == 'B') &&
            (s[1] == 0 || isspace((unsigned char)s[1]) || s[1] == ',')) {
            const char *base = ieq(mnem, "ADD") ? "ADD" :
                               ieq(mnem, "SUB") ? "SUB" :
                               ieq(mnem, "ADC") ? "ADC" : "SBC";
            snprintf(mnem, 32, "%s%c", base, r);
            s++;
            if (*s == ',') s++;
            while (*s && isspace((unsigned char)*s)) s++;
            memmove(operand, s, strlen(s) + 1);
        }
    }
    strncpy(mac_argline, operand, sizeof(mac_argline) - 1);
    mac_argline[sizeof(mac_argline) - 1] = 0;
    if (!keep_full(mnem)) {
        char *sp = operand;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        *sp = 0;
    }

    if (!assembling()) {
        list_hide = 1;
        if (ieq(mnem, "IFEQ")) push_if(0, operand);
        else if (ieq(mnem, "IFNE")) push_if(1, operand);
        else if (ieq(mnem, "IFGE")) push_if(2, operand);
        else if (ieq(mnem, "IFLE")) push_if(3, operand);
        else if (ieq(mnem, "IFGT")) push_if(4, operand);
        else if (ieq(mnem, "IFLT")) push_if(5, operand);
        else if (ieq(mnem, "IFC")) push_ifc(1, operand);
        else if (ieq(mnem, "IFNC")) push_ifc(0, operand);
        else if (ieq(mnem, "ELSE")) {
            if (ifsp > 0 && !ifs[ifsp - 1].saw_else) {
                if (ifs[ifsp - 1].parent_on) ifs[ifsp - 1].taken = !ifs[ifsp - 1].taken;
                ifs[ifsp - 1].saw_else = 1;
            }
        } else if (ieq(mnem, "ENDC")) {
            if (ifsp > 0) ifsp--;
        } else if (is_macr(mnem)) {
            mac_skip = 1;
        }
        return 1;
    }

    if (ieq(mnem, "IFEQ")) { push_if(0, operand); return 1; }
    if (ieq(mnem, "IFNE")) { push_if(1, operand); return 1; }
    if (ieq(mnem, "IFGE")) { push_if(2, operand); return 1; }
    if (ieq(mnem, "IFLE")) { push_if(3, operand); return 1; }
    if (ieq(mnem, "IFGT")) { push_if(4, operand); return 1; }
    if (ieq(mnem, "IFLT")) { push_if(5, operand); return 1; }
    if (ieq(mnem, "IFC")) { push_ifc(1, operand); return 1; }
    if (ieq(mnem, "IFNC")) { push_ifc(0, operand); return 1; }
    if (ieq(mnem, "ELSE")) {
        if (ifsp > 0 && !ifs[ifsp - 1].saw_else) {
            ifs[ifsp - 1].taken = !ifs[ifsp - 1].taken;
            ifs[ifsp - 1].saw_else = 1;
        }
        return 1;
    }
    if (ieq(mnem, "ENDC")) {
        if (ifsp > 0) ifsp--;
        return 1;
    }
    if (is_macr(mnem)) {
        if (!label || !label[0]) { fprintf(stderr, "macro needs a name\n"); errors++; return 1; }
        int idx = find_macro(label);
        if (idx < 0) {
            if (nmac >= MAXMAC) { fprintf(stderr, "macro table full\n"); errors++; return 1; }
            idx = nmac++;
            memset(&macros[idx], 0, sizeof(macros[idx]));
            strncpy(macros[idx].name, label, MAXNAME - 1);
        }
        macros[idx].nline = 0;
        mac_def = idx;
        return 1;
    }
    if (ieq(mnem, "OPT")) {
        list_plain = 1;
        list_force_show = 1;
        if (strncasecmp(operand, "NOL", 3) == 0) list_on = 0;
        else if (strcasecmp(operand, "6309") == 0) cpu_6309 = 1;
        else if (strcasecmp(operand, "6809") == 0) cpu_6309 = 0;
        else if (operand[0] == 'L' || operand[0] == 'l') list_on = 1;
        return 1;
    }
    if (ieq(mnem, "LIST")) { list_on = 1; list_plain = 1; return 1; }
    if (ieq(mnem, "NOLIST") || ieq(mnem, "NOL")) { list_on = 0; list_plain = 1; return 1; }

    int sc = sec_of(mnem);
    if (sc >= 0) {
        if (label && label[0]) sc = ensure_named(label);
        secs[sc].explicit = 1;
        switch_sec(sc);
        return 1;
    }
    {
        int mi = find_macro(mnem);
        if (mi >= 0) {
            /* `EDGWAT CALL WAITZ` — the label is the macro line, not a
             * line inside the expansion. */
            if (label) {
                ensure_sec();
                note_label(label);
            }
            mac_invoke = mi;
            return 2;
        }
    }

    if (emits_code(mnem)) {
        ensure_sec();
        snap_star();
    }
    if (label && !ieq(mnem, "EQU") && !ieq(mnem, "SET") && !ieq(mnem, "ORG"))
        note_label(label);

    if (ieq(mnem, "NAM")) {
        if (operand[0]) {
            strncpy(module, operand, sizeof(module) - 1);
            char *sp = module;
            while (*sp && !isspace((unsigned char)*sp)) sp++;
            *sp = 0;
        }
        return 1;
    }
    if (ieq(mnem, "TTL") || ieq(mnem, "STL") || ieq(mnem, "SPC") || ieq(mnem, "PAGE"))
        return 1;
    if (ieq(mnem, "ENDM")) return 1;
    if (ieq(mnem, "SETDP")) {
        val_t v = operand[0] ? eval_expr(operand) : (val_t){0, K_ABS, -1, {0}};
        int page = v.value & 0xFFFF;
        /* SETDP $84 is a page number. SETDP $8400 is the window base. */
        if (page > 0xFF) page >>= 8;
        direct_page = page & 0xFF;
        return 1;
    }
    if (ieq(mnem, "GLOBAL")) { split_names(operand, globals, &nglob); return 1; }
    if (ieq(mnem, "EXTERN")) { split_names(operand, externs, &nextn); return 1; }
    if (ieq(mnem, "END")) {
        flush_ck();
        return 0;
    }
    if (ieq(mnem, "ORG")) {
        ensure_sec();
        flush_ck();
        close_span(cur);
        val_t v = eval_expr(operand);
        S()->abs = 1;
        S()->org = v.value & 0xFFFF;
        S()->pc = v.value & 0xFFFF;
        open_span(cur);
        snap_star();
        if (label) note_label(label);
        return 1;
    }
    if (ieq(mnem, "EQU") || ieq(mnem, "SET")) {
        if (label) {
            val_t v = eval_expr(operand);
            int sec = -1;
            int kind = kind_of(v, &sec);
            if (ieq(mnem, "SET")) set_sym(label, v.value, kind, sec);
            else define_sym(label, v.value, kind, sec);
            list_equ = 1;
            list_equ_v = v.value & 0xFFFF;
            /* GLOBAL EQU * (KP_SER) and GLOBAL absolute EQUs (stdin)
             * are exports. A label on an instruction already goes
             * through note_label; EQU does not. */
            if (pass == 2 && has_name(globals, nglob, label)) {
                if (nmeta >= MAXMETA) { errors++; return 1; }
                meta_t *m = &metas[nmeta++];
                memset(m, 0, sizeof(*m));
                m->kind = 2;
                if (kind == K_REL && sec >= 0) {
                    m->sec = sec;
                    m->off = v.value & 0xFFFF;
                } else {
                    m->sec = 0;
                    m->off = v.value & 0xFFFF;
                }
                strncpy(m->name, label, MAXNAME - 1);
            }
        }
        return 1;
    }
    if (ieq(mnem, "RMB")) {
        flush_ck();
        list_before = 1;
        val_t v = eval_expr(operand);
        add_pc(v.value & 0xFFFF);
        return 1;
    }
    if (ieq(mnem, "BSZ")) {
        val_t v = eval_expr(operand);
        int n = v.value & 0xFFFF;
        for (int i = 0; i < n; i++) lit(0);
        return 1;
    }
    if (ieq(mnem, "FCB")) {
        char *p = operand;
        char item[128];
        while (next_item(&p, item, sizeof(item))) {
            val_t v = eval_expr(item);
            lit(v.value & 0xFF);
        }
        return 1;
    }
    if (ieq(mnem, "FDB")) {
        char *p = operand;
        char item[128];
        int any = 0;
        while (next_item(&p, item, sizeof(item))) {
            emit_addr(eval_expr(item));
            any = 1;
        }
        if (!any && operand[0]) emit_addr(eval_expr(operand));
        return 1;
    }
    if (ieq(mnem, "FCC")) {
        char *s = operand;
        while (*s && isspace((unsigned char)*s)) s++;
        if (isdigit((unsigned char)*s)) {
            int count = atoi(s);
            char *c = strchr(s, ',');
            /* Spaces after the comma are string data (`FCC 20,   W.D.`). */
            const char *text = c ? c + 1 : "";
            for (int i = 0; i < count; i++)
                lit(text[i] ? (unsigned char)text[i] : ' ');
        } else if (*s) {
            char q = *s++;
            while (*s && *s != q) lit((unsigned char)*s++);
        }
        return 1;
    }
    /* 6800 names the 6809 listing expands to more than one instruction. */
    if (ieq(mnem, "TAB")) {
        const opc_t *a = find_op("TFR");
        const opc_t *b = find_op("TSTA");
        if (a) do_instr(a, "TFR", "A,B");
        if (b) do_instr(b, "TSTA", "");
        return 1;
    }
    if (ieq(mnem, "CBA")) {
        const opc_t *a = find_op("PSHS");
        const opc_t *b = find_op("CMPA");
        if (a) do_instr(a, "PSHS", "B");
        if (b) do_instr(b, "CMPA", ",S+");
        return 1;
    }
    /* 6309 CLRD is 10 4F. Without OPT 6309 this stays CLRB, CLRA. */
    if (ieq(mnem, "CLRD") && !cpu_6309) {
        const opc_t *a = find_op("CLRB");
        const opc_t *b = find_op("CLRA");
        if (a) do_instr(a, "CLRB", "");
        if (b) do_instr(b, "CLRA", "");
        return 1;
    }
    /* 6800 inherent ops the 6809 listing expands. ABA is PSHS B / ADDA ,S+. */
    if (ieq(mnem, "ABA") || ieq(mnem, "SBA")) {
        const opc_t *a = find_op("PSHS");
        const opc_t *b = find_op(ieq(mnem, "ABA") ? "ADDA" : "SUBA");
        if (a) do_instr(a, "PSHS", "B");
        if (b) do_instr(b, ieq(mnem, "ABA") ? "ADDA" : "SUBA", ",S+");
        return 1;
    }
    if (ieq(mnem, "TBA")) {
        const opc_t *a = find_op("TFR");
        const opc_t *b = find_op("TSTA");
        if (a) do_instr(a, "TFR", "B,A");
        if (b) do_instr(b, "TSTA", "");
        return 1;
    }
    const opc_t *op = find_op(mnem);
    if (op) { do_instr(op, mnem, operand); return 1; }
    fprintf(stderr, "unknown mnemonic: %s\n", mnem);
    errors++;
    return 1;
}

static void init_exe_dir(void) {
    char buf[1024];
#if defined(__APPLE__)
    uint32_t sz = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return;
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) return;
    buf[n] = 0;
#endif
    char real[1024];
    if (!realpath(buf, real)) return;
    char *sl = strrchr(real, '/');
    if (!sl) return;
    *sl = 0;
    snprintf(exe_directory, sizeof(exe_directory), "%s", real);
    have_exe_dir = 1;
}

static const char *base_of(const char *spec) {
    const char *b = spec;
    for (const char *p = spec; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static int scan_dir_ci(const char *dir, const char *base, char *out, size_t nout) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    int ok = 0;
    while ((de = readdir(d))) {
        if (strcasecmp(de->d_name, base) == 0) {
            snprintf(out, nout, "%s/%s", dir, de->d_name);
            ok = 1;
            break;
        }
    }
    closedir(d);
    return ok && access(out, R_OK) == 0;
}

static int find_include(const char *from, const char *spec, char *out, size_t nout) {
    char dir[512];
    strncpy(dir, from, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    else strcpy(dir, ".");
    const char *base = base_of(spec);
    snprintf(out, nout, "%s/%s", dir, spec);
    if (access(out, R_OK) == 0) return 1;
    if (scan_dir_ci(dir, base, out, nout)) return 1;
    if (have_exe_dir) {
        const char *rels[] = { "../WINTEK", "../lib/include" };
        for (size_t i = 0; i < 2; i++) {
            char joined[512], real[512];
            snprintf(joined, sizeof(joined), "%s/%s", exe_directory, rels[i]);
            if (!realpath(joined, real)) continue;
            if (scan_dir_ci(real, base, out, nout)) return 1;
        }
    }
    return 0;
}

static int line_is(const char *line, const char *mnem) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t n = strlen(mnem);
    /* ENDM is written in column 0 as well as after a label. */
    if (strncasecmp(p, mnem, n) == 0 && (p[n] == 0 || isspace((unsigned char)p[n])))
        return 1;
    if (*p && *p != ' ' && *p != '\t') {
        while (*p && !isspace((unsigned char)*p)) p++;
    }
    while (*p && isspace((unsigned char)*p)) p++;
    if (strncasecmp(p, mnem, n) != 0) return 0;
    return p[n] == 0 || isspace((unsigned char)p[n]);
}

static void write_list(const char *raw) {
    if (!lstf || pass != 2 || list_hide) return;
    char buf[MAXLINE];
    strncpy(buf, raw ? raw : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    if (list_plain) {
        fprintf(lstf, "%s\n", buf);
        return;
    }
    int pc_after = (cur >= 0) ? (S()->pc & 0xFFFF) : 0;
    int addr = list_equ ? (list_equ_v & 0xFFFF)
                        : ((list_n > 0 || list_before) ? (list_pc & 0xFFFF) : pc_after);
    fprintf(lstf, "     %04X", addr);
    if (!list_equ && list_n > 0) {
        int show = list_n > 4 ? 3 : list_n;
        for (int i = 0; i < show; i++) fprintf(lstf, " %02X", list_b[i]);
        if (list_mark && list_mark_at >= 0 && list_mark_at < show) fputc(list_mark, lstf);
    }
    fprintf(lstf, "\t%s\n", buf);
}

/* One macro argument. Parentheses keep internal spaces
 * (`Z (HAVE A NICE DAY)`). A space that is not the gap before the
 * next comma ends the list, so `PUSHU 5,2,1,1  PAUSE .5 SEC` is
 * four arguments and the remark is not \3. */
static int mac_field(const char **pp, char *tok, size_t ntok) {
    const char *p = *pp;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p || *p == '*' || *p == ';') {
        *pp = p;
        return 0;
    }
    size_t i = 0;
    if (*p == '(') {
        int depth = 0;
        while (*p) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            if (i + 1 < ntok) tok[i++] = *p;
            p++;
            if (depth <= 0) break;
        }
        /* The parentheses group the argument. They are not text:
         * `Z (HAVE A NICE DAY)` stores `HAVE A NICE DAY`. */
        if (i >= 2 && tok[0] == '(' && tok[i - 1] == ')') {
            memmove(tok, tok + 1, (size_t)(i - 2));
            i -= 2;
        }
    } else if (*p != ',') {
        while (*p && *p != ',' && !isspace((unsigned char)*p) && i + 1 < ntok)
            tok[i++] = *p++;
    }
    tok[i] = 0;
    while (*p && isspace((unsigned char)*p)) p++;
    int more = (*p == ',');
    if (more) p++;
    else while (*p) p++;
    *pp = p;
    return i > 0 || more;
}

static int count_mac_args(const char *line) {
    const char *p = line ? line : "";
    int n = 0;
    char tok[64];
    while (mac_field(&p, tok, sizeof(tok))) n++;
    return n;
}

static void mac_subst(char *dst, size_t ndst, const char *src) {
    char args[16][64];
    int narg = 0;
    char tmp[512];
    strncpy(tmp, mac_argline, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    const char *p = tmp;
    while (narg < 16 && mac_field(&p, args[narg], sizeof(args[0]))) narg++;
    size_t k = 0;
    for (size_t i = 0; src[i] && k + 1 < ndst; i++) {
        int param = -1;
        if (src[i] == '\\' && isdigit((unsigned char)src[i + 1]))
            param = src[++i] - '0';
        else if (src[i] == '&' && src[i + 1] == '\\' && isdigit((unsigned char)src[i + 2])) {
            /* WINTEK `&\2` is the parameter, not a literal ampersand. */
            param = src[i + 2] - '0';
            i += 2;
        }
        if (param >= 0) {
            const char *rep = (param < narg) ? args[param] : "";
            for (size_t j = 0; rep[j] && k + 1 < ndst; j++) dst[k++] = rep[j];
        } else {
            dst[k++] = src[i];
        }
    }
    dst[k] = 0;
}

static int feed_line(char *line);
static int read_file(const char *path);

static void expand_macro(int idx) {
    if (mac_depth > 4) { fprintf(stderr, "macro nested too deep\n"); errors++; return; }
    mac_depth++;
    char saved_args[512];
    strncpy(saved_args, mac_argline, sizeof(saved_args) - 1);
    saved_args[sizeof(saved_args) - 1] = 0;
    int saved_narg = mac_narg;
    mac_narg = count_mac_args(saved_args);
    int nline = macros[idx].nline;
    for (int i = 0; i < nline; i++) {
        char body[MAXLINE];
        strncpy(mac_argline, saved_args, sizeof(mac_argline) - 1);
        mac_subst(body, sizeof(body), macros[idx].body[i]);
        feed_line(body);
    }
    strncpy(mac_argline, saved_args, sizeof(mac_argline) - 1);
    mac_narg = saved_narg;
    mac_depth--;
}

static int include_spec(const char *line, char *tok, size_t ntok) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    int n = 0;
    if (strncasecmp(p, "INCLUDE", 7) == 0 && (p[7] == 0 || isspace((unsigned char)p[7]))) n = 7;
    else if (strncasecmp(p, "INCL", 4) == 0 && (p[4] == 0 || isspace((unsigned char)p[4]))) n = 4;
    else return 0;
    p += n;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t i = 0;
    if (*p == '\'' || *p == '"') {
        char q = *p++;
        while (*p && *p != q && i + 1 < ntok) tok[i++] = *p++;
    } else {
        while (*p && !isspace((unsigned char)*p) && i + 1 < ntok) tok[i++] = *p++;
    }
    tok[i] = 0;
    return 1;
}

static int feed_line(char *line) {
    char *z = strchr(line, '\x1a');
    if (z) *z = 0;
    if (mac_skip) {
        if (line_is(line, "ENDM")) mac_skip = 0;
        return 1;
    }
    if (mac_def >= 0) {
        if (line_is(line, "ENDM")) { mac_def = -1; return 1; }
        mac_t *m = &macros[mac_def];
        if (m->nline < MAXMACL) {
            strncpy(m->body[m->nline], line, MAXLINE - 1);
            m->body[m->nline][MAXLINE - 1] = 0;
            size_t n = strlen(m->body[m->nline]);
            while (n && (m->body[m->nline][n - 1] == '\n' || m->body[m->nline][n - 1] == '\r'))
                m->body[m->nline][--n] = 0;
            m->nline++;
        } else {
            fprintf(stderr, "macro too long\n");
            errors++;
        }
        return 1;
    }
    char spec[256];
    if (include_spec(line, spec, sizeof(spec))) {
        if (!assembling()) return 1;
        int show = list_on;
        list_n = 0;
        list_mark = 0;
        list_mark_at = -1;
        list_equ = 0;
        list_before = 0;
        list_plain = 0;
        list_force_show = 0;
        list_hide = 0;
        list_pc = (cur >= 0) ? S()->pc : 0;
        if (pass == 2 && (show || list_force_show)) write_list(line);
        char inc[512];
        if (!find_include(curpath, spec, inc, sizeof(inc))) die("missing include: %s", spec);
        return read_file(inc);
    }
    int show = list_on;
    list_n = 0;
    list_mark = 0;
    list_mark_at = -1;
    list_equ = 0;
    list_before = 0;
    list_plain = 0;
    list_force_show = 0;
    list_hide = 0;
    list_pc = (cur >= 0) ? S()->pc : 0;
    mac_invoke = -1;
    /* assemble_line writes NULs into the buffer when it splits the label. */
    char work[MAXLINE];
    strncpy(work, line, sizeof(work) - 1);
    work[sizeof(work) - 1] = 0;
    int cont = assemble_line(work);
    if (pass == 2 && (show || list_force_show)) write_list(line);
    if (cont == 2 && mac_invoke >= 0) {
        int idx = mac_invoke;
        expand_macro(idx);
        return 1;
    }
    return cont;
}

static int read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("can't open input file: %s", path);
    char saved[512];
    strncpy(saved, curpath, sizeof(saved) - 1);
    saved[sizeof(saved) - 1] = 0;
    strncpy(curpath, path, sizeof(curpath) - 1);
    curpath[sizeof(curpath) - 1] = 0;
    char line[MAXLINE];
    int cont = 1;
    while (cont && fgets(line, sizeof(line), f)) cont = feed_line(line);
    fclose(f);
    strncpy(curpath, saved, sizeof(curpath) - 1);
    return cont;
}

/* A literal is marker-like when the linker would treat that byte as a
 * relocation. Those bytes have to sit in a raw shield, not in the scan. */
static int lit_is_marker(int b) {
    if (b == 0xC0) return 1;
    int sec = -1;
    if ((b & 0xF0) == 0xC0) sec = b - 0xC0;
    else if ((b & 0xF0) == 0x20 && (b & 0x0F) != 0) sec = b & 0x0F;
    if (sec < 1 || sec > 15) return 0;
    return secs[sec].size > 0;
}

static int emit_marker(const tok_t *t, uint8_t *out) {
    if (t->k == TK_C2) {
        out[0] = 0xC2;
        out[1] = (uint8_t)((t->a >> 8) & 0xFF);
        out[2] = (uint8_t)(t->a & 0xFF);
        return 3;
    }
    if (t->k == TK_C1) {
        int sec = t->a & 0x0F;
        out[0] = (uint8_t)(0xC0 | sec);
        out[1] = (uint8_t)((t->b >> 8) & 0xFF);
        out[2] = (uint8_t)(t->b & 0xFF);
        return 3;
    }
    if (t->k == TK_EXT) {
        out[0] = 0xC0;
        out[1] = (uint8_t)((t->a >> 8) & 0xFF);
        out[2] = (uint8_t)(t->a & 0xFF);
        return 3;
    }
    if (t->k == TK_D8) {
        int sec = t->a & 0x0F;
        out[0] = (uint8_t)(0x20 | sec);
        out[1] = (uint8_t)((t->b >> 8) & 0xFF);
        out[2] = (uint8_t)(t->b & 0xFF);
        return 3;
    }
    return 0;
}

/* Scanned bytes. After each marker, the count is one less than the
 * following literal run: the linker copies that many raw bytes and then
 * takes one more byte as an opcode, even if the opcode looks like Cx. */
static int emit_scanned(const tok_t *t, int n, uint8_t *out) {
    int k = 0, i = 0;
    while (i < n) {
        if (t[i].k == TK_L) {
            out[k++] = (uint8_t)t[i].a;
            i++;
            continue;
        }
        k += emit_marker(&t[i], out + k);
        i++;
        int j = i;
        while (j < n && t[j].k == TK_L) j++;
        int nlit = j - i;
        if (nlit <= 0) continue;
        out[k++] = (uint8_t)(nlit - 1);
        for (int s = 0; s < nlit; s++) out[k++] = (uint8_t)t[i + s].a;
        i = j;
    }
    return k;
}

static int encode_chunk(const chunk_t *ch, uint8_t *out) {
    const tok_t *toks = ch->toks;
    int n = ch->ntok;
    if (n <= 0) return 0;
    if (toks[0].k == TK_EXT) {
        const tok_t *body = toks + 1;
        int nb = n - 1;
        if (nb == 0) {
            out[0] = 0xC0;
            out[1] = (uint8_t)((toks[0].a >> 8) & 0xFF);
            out[2] = (uint8_t)(toks[0].a & 0xFF);
            return 3;
        }
        int rel = -1;
        for (int i = 0; i < nb; i++)
            if (body[i].k == TK_C1 || body[i].k == TK_C2 || body[i].k == TK_D8) { rel = i; break; }
        int split;
        if (rel >= 0) split = (rel > 0 && body[rel - 1].k == TK_L) ? rel - 1 : rel;
        else if (ch->closed_by_ext) split = nb - 1;
        else split = nb;
        uint8_t shield[MAXTOK];
        int ns = 0;
        for (int i = 0; i < split; i++) if (body[i].k == TK_L) shield[ns++] = (uint8_t)body[i].a;
        uint8_t tail[2048];
        int nt = emit_scanned(body + split, nb - split, tail);
        int k = 0;
        out[k++] = 0xC0;
        out[k++] = (uint8_t)((toks[0].a >> 8) & 0xFF);
        out[k++] = (uint8_t)(toks[0].a & 0xFF);
        out[k++] = (uint8_t)ns;
        memcpy(out + k, shield, (size_t)ns);
        k += ns;
        memcpy(out + k, tail, (size_t)nt);
        k += nt;
        return k;
    }
    int m = -1;
    for (int i = 0; i < n; i++) if (toks[i].k != TK_L) { m = i; break; }
    if (m < 0) {
        int k = 1;
        for (int i = 0; i < n; i++) out[k++] = (uint8_t)toks[i].a;
        int last = n ? (toks[n - 1].a & 0xFF) : 0;
        /* n-1 leaves the last byte to be scanned. A marker-like last
         * byte is shielded instead so LDB/CMPB immediates stay literals. */
        out[0] = (uint8_t)((n > 0 && lit_is_marker(last)) ? n : (n > 0 ? n - 1 : 0));
        return k;
    }
    if (m == 0) return emit_scanned(toks, n, out);
    int last = toks[m - 1].a & 0xFF;
    int raw_n = lit_is_marker(last) ? m : m - 1;
    int scan_from = raw_n;
    out[0] = (uint8_t)raw_n;
    int k = 1;
    for (int i = 0; i < raw_n; i++) out[k++] = (uint8_t)toks[i].a;
    k += emit_scanned(toks + scan_from, n - scan_from, out + k);
    return k;
}

static int chunk_pure(const chunk_t *c) {
    for (int i = 0; i < c->ntok; i++) if (c->toks[i].k != TK_L) return 0;
    return c->ntok > 0;
}

static void split_chunks(void) {
    int no = 0;
    for (int i = 0; i < nchunk; i++) {
        chunk_t curc = chunks[i];
        int guard = 0;
        for (;;) {
            uint8_t tmp[2048];
            int len = encode_chunk(&curc, tmp);
            if (len <= MAX_S2 || curc.ntok == 0 || guard++ > 64) {
                if (no < MAXCHUNK) split_hold[no++] = curc;
                break;
            }
            if (curc.toks[0].k == TK_EXT) {
                int cut = -1, nlit = 0;
                for (int t = 1; t < curc.ntok; t++) {
                    if (curc.toks[t].k != TK_L) { cut = t; break; }
                    nlit++;
                    if (nlit == 29) { cut = t + 1; break; }
                }
                if (cut < 0 || cut >= curc.ntok) {
                    if (no < MAXCHUNK) split_hold[no++] = curc;
                    break;
                }
                chunk_t head;
                memset(&head, 0, sizeof(head));
                head.sec = curc.sec;
                head.off = curc.off;
                head.abs = curc.abs;
                head.closed_by_ext = 1;
                head.ntok = cut;
                memcpy(head.toks, curc.toks, (size_t)cut * sizeof(tok_t));
                int consumed = 2;
                for (int t = 1; t < cut; t++) if (curc.toks[t].k == TK_L) consumed++;
                chunk_t rest;
                memset(&rest, 0, sizeof(rest));
                rest.sec = curc.sec;
                rest.off = (curc.off + consumed) & 0xFFFF;
                rest.abs = curc.abs;
                rest.closed_by_ext = curc.closed_by_ext;
                rest.ntok = curc.ntok - cut;
                memcpy(rest.toks, curc.toks + cut, (size_t)rest.ntok * sizeof(tok_t));
                if (no < MAXCHUNK) split_hold[no++] = head;
                curc = rest;
                continue;
            }
            if (curc.ntok > 1 && curc.toks[0].k != TK_EXT) {
                /* A relocation in the middle can still make a record
                 * longer than 33 data bytes. Cut on a token boundary
                 * whose head encodes in one record. */
                int best = 0;
                for (int t = 1; t < curc.ntok; t++) {
                    chunk_t trial;
                    memset(&trial, 0, sizeof(trial));
                    trial.sec = curc.sec;
                    trial.off = curc.off;
                    trial.ntok = t;
                    memcpy(trial.toks, curc.toks, (size_t)t * sizeof(tok_t));
                    uint8_t probe[128];
                    int plen = encode_chunk(&trial, probe);
                    if (plen > MAX_S2) break;
                    best = t;
                }
                if (best > 0 && best < curc.ntok) {
                    int consumed = 0;
                    for (int t = 0; t < best; t++)
                        consumed += (curc.toks[t].k == TK_L || curc.toks[t].k == TK_D8) ? 1 : 2;
                    chunk_t head;
                    memset(&head, 0, sizeof(head));
                    head.sec = curc.sec;
                    head.off = curc.off;
                    head.abs = curc.abs;
                    head.ntok = best;
                    memcpy(head.toks, curc.toks, (size_t)best * sizeof(tok_t));
                    chunk_t rest;
                    memset(&rest, 0, sizeof(rest));
                    rest.sec = curc.sec;
                    rest.off = (curc.off + consumed) & 0xFFFF;
                    rest.abs = curc.abs;
                    rest.ntok = curc.ntok - best;
                    memcpy(rest.toks, curc.toks + best, (size_t)rest.ntok * sizeof(tok_t));
                    if (no < MAXCHUNK) split_hold[no++] = head;
                    curc = rest;
                    continue;
                }
            }
            if (chunk_pure(&curc) && curc.ntok > 32) {
                int cut = 32;
                chunk_t head;
                memset(&head, 0, sizeof(head));
                head.sec = curc.sec;
                head.off = curc.off;
                head.abs = curc.abs;
                head.ntok = cut;
                memcpy(head.toks, curc.toks, (size_t)cut * sizeof(tok_t));
                chunk_t rest;
                memset(&rest, 0, sizeof(rest));
                rest.sec = curc.sec;
                rest.off = (curc.off + cut) & 0xFFFF;
                rest.abs = curc.abs;
                rest.ntok = curc.ntok - cut;
                memcpy(rest.toks, curc.toks + cut, (size_t)rest.ntok * sizeof(tok_t));
                if (no < MAXCHUNK) split_hold[no++] = head;
                curc = rest;
                continue;
            }
            if (no < MAXCHUNK) split_hold[no++] = curc;
            break;
        }
    }
    memcpy(chunks, split_hold, (size_t)no * sizeof(chunk_t));
    nchunk = no;
}

static void emit_objects(FILE *fo) {
    uint8_t name[64];
    int nl = 0;
    for (const char *p = module; *p && nl < 60; p++) name[nl++] = (uint8_t)*p;
    srec_emit(fo, SREC_S0, 0, name, nl);
    int non_psct = 0;
    for (int i = 0; i < nmeta; i++) {
        meta_t *m = &metas[i];
        if (m->kind == 0 || m->kind == 1 || m->kind == 3) {
            /* Absolute spans in the .lx goldens carry a 0 section byte. */
            int sid = (m->kind == 1) ? 0 : m->sec;
            uint8_t d[3] = { (uint8_t)sid, (uint8_t)((m->end >> 8) & 0xFF), (uint8_t)(m->end & 0xFF) };
            uint32_t addr = (m->kind == 0) ? 0 : (uint32_t)m->org;
            srec_emit(fo, SREC_S4, addr, d, 3);
            if (m->sec != SEC_PSCT) non_psct = 1;
        } else if (m->kind == 4) {
            uint8_t d[40];
            int n = 0;
            for (const char *p = m->name; *p && n < 40; p++) d[n++] = (uint8_t)*p;
            srec_emit(fo, SREC_S3, (uint32_t)(m->sec + 0x40), d, n);
        } else if (m->kind == 2) {
            uint8_t d[40];
            int n = 0;
            d[n++] = 0;
            d[n++] = 0;
            for (const char *p = m->name; *p && n < 40; p++) d[n++] = (uint8_t)*p;
            uint32_t addr = ((uint32_t)(m->off & 0xFFFF) << 8) | (uint32_t)(m->sec & 0xFF);
            srec_emit(fo, SREC_S8, addr, d, n);
        }
    }
    split_chunks();
    for (int i = 0; i < nchunk; i++) {
        chunk_t *ch = &chunks[i];
        /* A bare ORG program is an absolute image: raw S1 bytes.
         * A named section (ASCT VECTOR$ FDB 0) stays relocatable S2
         * even when that one word has no reloc. */
        if (ch->abs && chunk_pure(ch) && !secs[ch->sec].explicit) {
            uint8_t raw[MAXTOK];
            int n = ch->ntok;
            if (n > MAXTOK) n = MAXTOK;
            for (int b = 0; b < n; b++) raw[b] = (uint8_t)ch->toks[b].a;
            int pos = 0;
            while (pos < n) {
                int take = n - pos;
                if (take > 32) take = 32;
                srec_emit(fo, SREC_S1, (uint32_t)((ch->off + pos) & 0xFFFF), raw + pos, take);
                pos += take;
            }
            continue;
        }
        uint8_t payload[2048];
        int plen = encode_chunk(ch, payload);
        /* An ORG span is absolute. Its S2/S7 address uses section 0
         * even when the ORG was written inside DSCT (`VOX` vectors). */
        int sid = ch->abs ? 0 : (ch->sec & 0xFF);
        uint32_t addr = ((uint32_t)(ch->off & 0xFFFF) << 8) | (uint32_t)sid;
        if (ch->ntok && ch->toks[0].k == TK_EXT) {
            uint8_t nm[40];
            int n = 0;
            for (const char *p = ch->toks[0].name; *p && n < 40; p++) nm[n++] = (uint8_t)*p;
            srec_emit(fo, SREC_S7, addr, nm, n);
        }
        srec_emit(fo, SREC_S2, addr, payload, plen);
    }
    if (non_psct) {
        uint8_t d[2] = { 0x02, 0x08 };
        srec_emit(fo, SREC_SD, 0, d, 2);
    }
    srec_emit(fo, SREC_S9, 0, NULL, 0);
}

static void reset_pass2(void) {
    for (int i = 0; i < MAXSEC; i++) {
        secs[i].pc = 0;
        secs[i].span_open = 0;
        secs[i].announced = 0;
        /* Pass 1 ORG leaves the section absolute. The bytes before
         * that ORG are relative and have to be relative again on pass 2. */
        secs[i].abs = 0;
    }
    cur = -1;
    nmeta = 0;
    nchunk = 0;
    ck = NULL;
    ifsp = 0;
    star_valid = 0;
    direct_page = 0;
    cpu_6309 = 0;
    list_on = 1;
    mac_def = -1;
    mac_skip = 0;
    for (int i = 0; i < nsym; i++) if (syms[i].is_set) {
        syms[i].value = 0;
        syms[i].kind = K_ABS;
        syms[i].sec = -1;
    }
}

int main(int argc, char **argv) {
    const char *listing = NULL;
    const char *output = NULL;
    int noheader = 0;
    const char *files[64];
    int nfiles = 0;
    if (argc < 2) usage(argv[0]);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) listing = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "-nh") == 0) noheader = 1;
        else if (argv[i][0] == '-') usage(argv[0]);
        else if (nfiles < 64) files[nfiles++] = argv[i];
    }
    if (nfiles == 0) usage(argv[0]);
    const char *infile = files[0];
    if (!noheader) {
        banner("Binary Systems, Inc. 6809 Assembler", VERSION, COPYRIGHT);
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        printf("%02d/%02d/%02d    %02d:%02d:%02d\n",
               tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100,
               tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    chunks = calloc((size_t)MAXCHUNK, sizeof(chunk_t));
    split_hold = calloc((size_t)MAXCHUNK, sizeof(chunk_t));
    if (!chunks || !split_hold) die("out of memory");
    init_exe_dir();
    for (int i = 0; i < MAXSEC; i++) secs[i].org = -1;

    pass = 1;
    for (int f = 0; f < nfiles; f++) read_file(files[f]);
    for (int i = 0; i < nsec; i++) {
        if (!secs[i].abs && secs[i].pc > secs[i].size) secs[i].size = secs[i].pc;
    }

    char outname[512];
    if (!output) {
        strncpy(outname, infile, sizeof(outname) - 5);
        outname[sizeof(outname) - 5] = 0;
        char *dot = strrchr(outname, '.');
        if (dot) *dot = 0;
        strcat(outname, ".rel");
        output = outname;
    }
    if (listing) {
        lstf = fopen(listing, "w");
        if (!lstf) warn("can't open listing file");
    }
    FILE *fo = fopen(output, "w");
    if (!fo) die("can't open output file: %s", output);

    reset_pass2();
    pass = 2;
    for (int f = 0; f < nfiles; f++) read_file(files[f]);
    flush_ck();
    if (cur >= 0) close_span(cur);
    emit_objects(fo);
    fclose(fo);
    if (lstf) fclose(lstf);

    if (errors) fprintf(stderr, "Errors detected.  Assembly aborted.\n");
    printf("Assembled %s -> %s (relocatable S-record object)\n", infile, output);
    if (listing) printf("Listing: %s\n", listing);
    return errors ? 1 : 0;
}
