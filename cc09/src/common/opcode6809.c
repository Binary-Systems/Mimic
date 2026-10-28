/* opcode6809.c - starter table for 6809 mnemonics (expand as needed) */
#include "opcode6809.h"
#include <string.h>
#include <strings.h>

static const op_t table[] = {
    /* inherent */
    {"ABX", 0x3A, AM_INH, 3, 1},
    {"ASLA",0x48, AM_INH, 2, 1},
    {"ASLB",0x58, AM_INH, 2, 1},
    {"ASRA",0x47, AM_INH, 2, 1},
    {"ASRB",0x57, AM_INH, 2, 1},
    {"CLRA",0x4F, AM_INH, 2, 1},
    {"CLRB",0x5F, AM_INH, 2, 1},
    {"CLRD",0x4F5F, AM_INH, 4, 2}, /* pseudo sometimes */
    {"COMA",0x43, AM_INH, 2, 1},
    {"COMB",0x53, AM_INH, 2, 1},
    {"DAA", 0x19, AM_INH, 2, 1},
    {"DECA",0x4A, AM_INH, 2, 1},
    {"DECB",0x5A, AM_INH, 2, 1},
    {"INCA",0x4C, AM_INH, 2, 1},
    {"INCB",0x5C, AM_INH, 2, 1},
    {"LSLA",0x48, AM_INH, 2, 1},
    {"LSLB",0x58, AM_INH, 2, 1},
    {"LSRA",0x44, AM_INH, 2, 1},
    {"LSRB",0x54, AM_INH, 2, 1},
    {"MUL", 0x3D, AM_INH, 11,1},
    {"NEGA",0x40, AM_INH, 2, 1},
    {"NEGB",0x50, AM_INH, 2, 1},
    {"NOP", 0x12, AM_INH, 2, 1},
    {"ROLA",0x49, AM_INH, 2, 1},
    {"ROLB",0x59, AM_INH, 2, 1},
    {"RORA",0x46, AM_INH, 2, 1},
    {"RORB",0x56, AM_INH, 2, 1},
    {"RTI", 0x3B, AM_INH, 6, 1},
    {"RTS", 0x39, AM_INH, 5, 1},
    {"SEX", 0x1D, AM_INH, 2, 1},
    {"SWI", 0x3F, AM_INH, 19,1},
    {"SWI2",0x103F, AM_INH, 20,2},
    {"SWI3",0x113F, AM_INH, 20,2},
    {"SYNC",0x13, AM_INH, 2, 1},
    {"TSTA",0x4D, AM_INH, 2, 1},
    {"TSTB",0x5D, AM_INH, 2, 1},
    /* branches short */
    {"BRA", 0x20, AM_REL, 3, 2},
    {"LBRA",0x16, AM_LREL, 5, 3},
    {"BRN", 0x21, AM_REL, 3, 2},
    {"LBRN",0x1021, AM_LREL, 5, 4},
    {"BHI", 0x22, AM_REL, 3, 2},
    {"LBHI",0x1022, AM_LREL, 5, 4},
    {"BLS", 0x23, AM_REL, 3, 2},
    {"LBLS",0x1023, AM_LREL, 5, 4},
    {"BCC", 0x24, AM_REL, 3, 2},
    {"LBCC",0x1024, AM_LREL, 5, 4},
    {"BHS", 0x24, AM_REL, 3, 2},
    {"LBHS",0x1024, AM_LREL, 5, 4},
    {"BCS", 0x25, AM_REL, 3, 2},
    {"LBCS",0x1025, AM_LREL, 5, 4},
    {"BLO", 0x25, AM_REL, 3, 2},
    {"LBLO",0x1025, AM_LREL, 5, 4},
    {"BNE", 0x26, AM_REL, 3, 2},
    {"LBNE",0x1026, AM_LREL, 5, 4},
    {"BEQ", 0x27, AM_REL, 3, 2},
    {"LBEQ",0x1027, AM_LREL, 5, 4},
    {"BVC", 0x28, AM_REL, 3, 2},
    {"LBVC",0x1028, AM_LREL, 5, 4},
    {"BVS", 0x29, AM_REL, 3, 2},
    {"LBVS",0x1029, AM_LREL, 5, 4},
    {"BPL", 0x2A, AM_REL, 3, 2},
    {"LBPL",0x102A, AM_LREL, 5, 4},
    {"BMI", 0x2B, AM_REL, 3, 2},
    {"LBMI",0x102B, AM_LREL, 5, 4},
    {"BGE", 0x2C, AM_REL, 3, 2},
    {"LBGE",0x102C, AM_LREL, 5, 4},
    {"BLT", 0x2D, AM_REL, 3, 2},
    {"LBLT",0x102D, AM_LREL, 5, 4},
    {"BGT", 0x2E, AM_REL, 3, 2},
    {"LBGT",0x102E, AM_LREL, 5, 4},
    {"BLE", 0x2F, AM_REL, 3, 2},
    {"LBLE",0x102F, AM_LREL, 5, 4},
    {"BSR", 0x8D, AM_REL, 7, 2},
    {"LBSR",0x17, AM_LREL, 9, 3},
    /* simple immediate/direct/ext mostly for A/B/D/X/Y etc */
    {"ADDA",0x8B, AM_IMM, 2, 2},
    {"ADDB",0xCB, AM_IMM, 2, 2},
    {"ADDD",0xC3, AM_IMM, 4, 3},
    {"ANDA",0x84, AM_IMM, 2, 2},
    {"ANDB",0xC4, AM_IMM, 2, 2},
    {"ANDCC",0x1C, AM_IMM, 3, 2},
    {"BITA",0x85, AM_IMM, 2, 2},
    {"BITB",0xC5, AM_IMM, 2, 2},
    {"CMPA",0x81, AM_IMM, 2, 2},
    {"CMPB",0xC1, AM_IMM, 2, 2},
    {"CMPD",0x1083,AM_IMM, 5, 4},
    {"CMPS",0x118C,AM_IMM, 5, 4},
    {"CMPU",0x1183,AM_IMM, 5, 4},
    {"CMPX",0x8C, AM_IMM, 4, 3},
    {"CMPY",0x108C,AM_IMM, 5, 4},
    {"EORA",0x88, AM_IMM, 2, 2},
    {"EORB",0xC8, AM_IMM, 2, 2},
    {"LDA", 0x86, AM_IMM, 2, 2},
    {"LDB", 0xC6, AM_IMM, 2, 2},
    {"LDD", 0xCC, AM_IMM, 3, 3},
    {"LDS", 0x10CE,AM_IMM, 4, 4},
    {"LDU", 0xCE, AM_IMM, 3, 3},
    {"LDX", 0x8E, AM_IMM, 3, 3},
    {"LDY", 0x108E,AM_IMM, 4, 4},
    {"ORA", 0x8A, AM_IMM, 2, 2},
    {"ORB", 0xCA, AM_IMM, 2, 2},
    {"ORCC",0x1A, AM_IMM, 3, 2},
    {"SBCA",0x82, AM_IMM, 2, 2},
    {"SBCB",0xC2, AM_IMM, 2, 2},
    {"SUBA",0x80, AM_IMM, 2, 2},
    {"SUBB",0xC0, AM_IMM, 2, 2},
    {"SUBD",0x83, AM_IMM, 4, 3},
    /* index examples - full postbyte logic is complex; table gives base */
    {"LDA", 0xA6, AM_IX0, 4, 2},  /* base; real size depends on postbyte */
    {"STA", 0xA7, AM_IX0, 4, 2},
    {"JMP", 0x6E, AM_IX0, 3, 2},
    {"JSR", 0xAD, AM_IX0, 7, 2},
    {"LEAX",0x30, AM_IX0, 4, 2},
    {"LEAY",0x31, AM_IX0, 4, 2},
    {"LEAS",0x32, AM_IX0, 4, 2},
    {"LEAU",0x33, AM_IX0, 4, 2},
    /* push/pull */
    {"PSHS",0x34, AM_RLIST, 5, 2}, /* +1 per reg */
    {"PULS",0x35, AM_RLIST, 5, 2},
    {"PSHU",0x36, AM_RLIST, 5, 2},
    {"PULU",0x37, AM_RLIST, 5, 2},
    /* tfr/exg */
    {"TFR", 0x1F, AM_TFR,  6, 2},
    {"EXG", 0x1E, AM_TFR,  6, 2},
    /* stores (primarily extended for runtime ACIA $F9xx; dir/ix can be added) */
    {"STA", 0xB7, AM_EXT, 4, 3},
    {"STB", 0xF7, AM_EXT, 4, 3},
    {"STD", 0xFD, AM_EXT, 5, 3},
    {"STX", 0xBF, AM_EXT, 5, 3},
    {"STY", 0x10BF, AM_EXT, 6, 4},
    {"STU", 0xFF, AM_EXT, 5, 3},
    {"STS", 0x10FF, AM_EXT, 6, 4},
    /* additional load/ext forms used by runtime (chario etc) */
    {"LDA", 0xB6, AM_EXT, 4, 3},
    {"LDB", 0xF6, AM_EXT, 4, 3},
    {"LDD", 0xFC, AM_EXT, 5, 3},
    {"LDX", 0xBE, AM_EXT, 5, 3},
    {"LDY", 0x10BE, AM_EXT, 6, 4},
    {"LDU", 0xFE, AM_EXT, 5, 3},
    {"LDS", 0x10FE, AM_EXT, 6, 4},
    {"BITA", 0xB5, AM_EXT, 4, 3},
    {"BITB", 0xF5, AM_EXT, 4, 3},
    {"ANDB", 0xF4, AM_EXT, 4, 3},
    {"ANDB", 0xC4, AM_IMM, 2, 2}, /* imm variant already needed */
    {"SUBA", 0xB0, AM_EXT, 4, 3},
    {"SUBA", 0x80, AM_IMM, 2, 2},
    {"CMPB", 0xF1, AM_EXT, 4, 3},
    {"CMPB", 0xC1, AM_IMM, 2, 2},
    {"LSRB", 0x54, AM_INH, 2, 1},
    /* end sentinel */
    {NULL, 0, AM_INV, 0, 0}
};

const op_t *find_opcode(const char *mnem) {
    if (!mnem) return NULL;
    for (int i = 0; table[i].mnem; i++) {
        if (strcasecmp(table[i].mnem, mnem) == 0)
            return &table[i];
    }
    return NULL;
}

int is_valid_mnem(const char *mnem) {
    return find_opcode(mnem) != NULL;
}

int encode_instr(const char *mnem, amode_t am, uint8_t *out_op, uint8_t *out_post, int *out_len) {
    const op_t *o = find_opcode(mnem);
    if (!o) return -1;
    *out_op = (uint8_t)(o->opcode & 0xff);
    *out_post = 0;
    *out_len = o->bytes;
    /* For page2/3 the high byte of opcode in table is used by caller */
    if (o->opcode > 0xff) {
        /* caller must emit high byte first for 10xx/11xx */
    }
    /* Indexed and auto need postbyte computation in assembler proper */
    return 0;
}
