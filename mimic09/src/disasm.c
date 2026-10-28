/* disasm.c — table-driven 6809/6309 disassembler for the monitor */

#include "disasm.h"
#include "cpu.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    M_IMPL, M_IMM8, M_IMM16, M_DIR, M_EXT, M_REL8, M_REL16,
    M_IDX, M_INH_A, M_INH_B, M_INH_D, M_PSH, M_TFR, M_SWI,
    M_IMM32_6309, M_IMMDIR, M_IMMIDX, M_IMMEXT, M_RR, M_TFM, M_BIT
} mode_t;

typedef struct {
    const char *mnem;
    mode_t mode;
    int bytes;   /* total including opcode */
} opinfo_t;

static const opinfo_t optab[256] = {
    /* 00 */ {"NEG", M_DIR, 2}, {"OIM", M_IMMDIR,3}, {"AIM", M_IMMDIR,3}, {"COM", M_DIR,2},
    /* 04 */ {"LSR", M_DIR,2}, {"EIM", M_IMMDIR,3}, {"ROR", M_DIR,2}, {"ASR", M_DIR,2},
    /* 08 */ {"ASL", M_DIR,2}, {"ROL", M_DIR,2}, {"DEC", M_DIR,2}, {"TIM", M_IMMDIR,3},
    /* 0C */ {"INC", M_DIR,2}, {"TST", M_DIR,2}, {"JMP", M_DIR,2}, {"CLR", M_DIR,2},

    /* 10 */ {"???", M_IMPL,1}, {"???", M_IMPL,1}, {"NOP", M_IMPL,1}, {"SYNC",M_IMPL,1},
    /* 14 */ {"SEXW",M_IMPL,1}, {"???", M_IMPL,1}, {"LBRA",M_REL16,3}, {"LBSR",M_REL16,3},
    /* 18 */ {"???", M_IMPL,1}, {"DAA", M_IMPL,1}, {"ORCC",M_IMM8,2}, {"???", M_IMPL,1},
    /* 1C */ {"ANDCC",M_IMM8,2}, {"SEX", M_IMPL,1}, {"EXG", M_TFR,2}, {"TFR", M_TFR,2},

    /* 20 */ {"BRA", M_REL8,2}, {"BRN", M_REL8,2}, {"BHI", M_REL8,2}, {"BLS", M_REL8,2},
    /* 24 */ {"BCC", M_REL8,2}, {"BCS", M_REL8,2}, {"BNE", M_REL8,2}, {"BEQ", M_REL8,2},
    /* 28 */ {"BVC", M_REL8,2}, {"BVS", M_REL8,2}, {"BPL", M_REL8,2}, {"BMI", M_REL8,2},
    /* 2C */ {"BGE", M_REL8,2}, {"BLT", M_REL8,2}, {"BGT", M_REL8,2}, {"BLE", M_REL8,2},

    /* 30 */ {"LEAX",M_IDX,2}, {"LEAY",M_IDX,2}, {"LEAS",M_IDX,2}, {"LEAU",M_IDX,2},
    /* 34 */ {"PSHS",M_PSH,2}, {"PULS",M_PSH,2}, {"PSHU",M_PSH,2}, {"PULU",M_PSH,2},
    /* 38 */ {"???", M_IMPL,1}, {"RTS", M_IMPL,1}, {"ABX", M_IMPL,1}, {"RTI", M_IMPL,1},
    /* 3C */ {"CWAI",M_IMM8,2}, {"MUL", M_IMPL,1}, {"???", M_IMPL,1}, {"SWI", M_SWI,1},

    /* 40 */ {"NEGA",M_INH_A,1}, {"???", M_IMPL,1}, {"???", M_IMPL,1}, {"COMA",M_INH_A,1},
    /* 44 */ {"LSRA",M_INH_A,1}, {"???", M_IMPL,1}, {"RORA",M_INH_A,1}, {"ASRA",M_INH_A,1},
    /* 48 */ {"ASLA",M_INH_A,1}, {"ROLA",M_INH_A,1}, {"DECA",M_INH_A,1}, {"???", M_IMPL,1},
    /* 4C */ {"INCA",M_INH_A,1}, {"TSTA",M_INH_A,1}, {"???", M_IMPL,1}, {"CLRA",M_INH_A,1},

    /* 50 */ {"NEGB",M_INH_B,1}, {"???", M_IMPL,1}, {"???", M_IMPL,1}, {"COMB",M_INH_B,1},
    /* 54 */ {"LSRB",M_INH_B,1}, {"???", M_IMPL,1}, {"RORB",M_INH_B,1}, {"ASRB",M_INH_B,1},
    /* 58 */ {"ASLB",M_INH_B,1}, {"ROLB",M_INH_B,1}, {"DECB",M_INH_B,1}, {"???", M_IMPL,1},
    /* 5C */ {"INCB",M_INH_B,1}, {"TSTB",M_INH_B,1}, {"???", M_IMPL,1}, {"CLRB",M_INH_B,1},

    /* 60 */ {"NEG", M_IDX,2}, {"OIM", M_IMMIDX,3}, {"AIM", M_IMMIDX,3}, {"COM", M_IDX,2},
    /* 64 */ {"LSR", M_IDX,2}, {"EIM", M_IMMIDX,3}, {"ROR", M_IDX,2}, {"ASR", M_IDX,2},
    /* 68 */ {"ASL", M_IDX,2}, {"ROL", M_IDX,2}, {"DEC", M_IDX,2}, {"TIM", M_IMMIDX,3},
    /* 6C */ {"INC", M_IDX,2}, {"TST", M_IDX,2}, {"JMP", M_IDX,2}, {"CLR", M_IDX,2},

    /* 70 */ {"NEG", M_EXT,3}, {"OIM", M_IMMEXT,4}, {"AIM", M_IMMEXT,4}, {"COM", M_EXT,3},
    /* 74 */ {"LSR", M_EXT,3}, {"EIM", M_IMMEXT,4}, {"ROR", M_EXT,3}, {"ASR", M_EXT,3},
    /* 78 */ {"ASL", M_EXT,3}, {"ROL", M_EXT,3}, {"DEC", M_EXT,3}, {"TIM", M_IMMEXT,4},
    /* 7C */ {"INC", M_EXT,3}, {"TST", M_EXT,3}, {"JMP", M_EXT,3}, {"CLR", M_EXT,3},

    /* 80 */ {"SUBA",M_IMM8,2}, {"CMPA",M_IMM8,2}, {"SBCA",M_IMM8,2}, {"SUBD",M_IMM16,3},
    /* 84 */ {"ANDA",M_IMM8,2}, {"BITA",M_IMM8,2}, {"LDA", M_IMM8,2}, {"???", M_IMPL,1},
    /* 88 */ {"EORA",M_IMM8,2}, {"ADCA",M_IMM8,2}, {"ORA", M_IMM8,2}, {"ADDA",M_IMM8,2},
    /* 8C */ {"CMPX",M_IMM16,3}, {"BSR", M_REL8,2}, {"LDX", M_IMM16,3}, {"???", M_IMPL,1},

    /* 90 */ {"SUBA",M_DIR,2}, {"CMPA",M_DIR,2}, {"SBCA",M_DIR,2}, {"SUBD",M_DIR,2},
    /* 94 */ {"ANDA",M_DIR,2}, {"BITA",M_DIR,2}, {"LDA", M_DIR,2}, {"STA", M_DIR,2},
    /* 98 */ {"EORA",M_DIR,2}, {"ADCA",M_DIR,2}, {"ORA", M_DIR,2}, {"ADDA",M_DIR,2},
    /* 9C */ {"CMPX",M_DIR,2}, {"JSR", M_DIR,2}, {"LDX", M_DIR,2}, {"STX", M_DIR,2},

    /* A0 */ {"SUBA",M_IDX,2}, {"CMPA",M_IDX,2}, {"SBCA",M_IDX,2}, {"SUBD",M_IDX,2},
    /* A4 */ {"ANDA",M_IDX,2}, {"BITA",M_IDX,2}, {"LDA", M_IDX,2}, {"STA", M_IDX,2},
    /* A8 */ {"EORA",M_IDX,2}, {"ADCA",M_IDX,2}, {"ORA", M_IDX,2}, {"ADDA",M_IDX,2},
    /* AC */ {"CMPX",M_IDX,2}, {"JSR", M_IDX,2}, {"LDX", M_IDX,2}, {"STX", M_IDX,2},

    /* B0 */ {"SUBA",M_EXT,3}, {"CMPA",M_EXT,3}, {"SBCA",M_EXT,3}, {"SUBD",M_EXT,3},
    /* B4 */ {"ANDA",M_EXT,3}, {"BITA",M_EXT,3}, {"LDA", M_EXT,3}, {"STA", M_EXT,3},
    /* B8 */ {"EORA",M_EXT,3}, {"ADCA",M_EXT,3}, {"ORA", M_EXT,3}, {"ADDA",M_EXT,3},
    /* BC */ {"CMPX",M_EXT,3}, {"JSR", M_EXT,3}, {"LDX", M_EXT,3}, {"STX", M_EXT,3},

    /* C0 */ {"SUBB",M_IMM8,2}, {"CMPB",M_IMM8,2}, {"SBCB",M_IMM8,2}, {"ADDD",M_IMM16,3},
    /* C4 */ {"ANDB",M_IMM8,2}, {"BITB",M_IMM8,2}, {"LDB", M_IMM8,2}, {"???", M_IMPL,1},
    /* C8 */ {"EORB",M_IMM8,2}, {"ADCB",M_IMM8,2}, {"ORB", M_IMM8,2}, {"ADDB",M_IMM8,2},
    /* CC */ {"LDD", M_IMM16,3}, {"LDQ", M_IMM32_6309,5}, {"LDU", M_IMM16,3}, {"???", M_IMPL,1},

    /* D0 */ {"SUBB",M_DIR,2}, {"CMPB",M_DIR,2}, {"SBCB",M_DIR,2}, {"ADDD",M_DIR,2},
    /* D4 */ {"ANDB",M_DIR,2}, {"BITB",M_DIR,2}, {"LDB", M_DIR,2}, {"STB", M_DIR,2},
    /* D8 */ {"EORB",M_DIR,2}, {"ADCB",M_DIR,2}, {"ORB", M_DIR,2}, {"ADDB",M_DIR,2},
    /* DC */ {"LDD", M_DIR,2}, {"STD", M_DIR,2}, {"LDU", M_DIR,2}, {"STU", M_DIR,2},

    /* E0 */ {"SUBB",M_IDX,2}, {"CMPB",M_IDX,2}, {"SBCB",M_IDX,2}, {"ADDD",M_IDX,2},
    /* E4 */ {"ANDB",M_IDX,2}, {"BITB",M_IDX,2}, {"LDB", M_IDX,2}, {"STB", M_IDX,2},
    /* E8 */ {"EORB",M_IDX,2}, {"ADCB",M_IDX,2}, {"ORB", M_IDX,2}, {"ADDB",M_IDX,2},
    /* EC */ {"LDD", M_IDX,2}, {"STD", M_IDX,2}, {"LDU", M_IDX,2}, {"STU", M_IDX,2},

    /* F0 */ {"SUBB",M_EXT,3}, {"CMPB",M_EXT,3}, {"SBCB",M_EXT,3}, {"ADDD",M_EXT,3},
    /* F4 */ {"ANDB",M_EXT,3}, {"BITB",M_EXT,3}, {"LDB", M_EXT,3}, {"STB", M_EXT,3},
    /* F8 */ {"EORB",M_EXT,3}, {"ADCB",M_EXT,3}, {"ORB", M_EXT,3}, {"ADDB",M_EXT,3},
    /* FC */ {"LDD", M_EXT,3}, {"STD", M_EXT,3}, {"LDU", M_EXT,3}, {"STU", M_EXT,3},
};

static const opinfo_t optab10[256] = {
    [0x20]={"LBRA",M_REL16,3}, [0x21]={"LBRN",M_REL16,3}, [0x22]={"LBHI",M_REL16,3},
    [0x23]={"LBLS",M_REL16,3}, [0x24]={"LBCC",M_REL16,3}, [0x25]={"LBCS",M_REL16,3},
    [0x26]={"LBNE",M_REL16,3}, [0x27]={"LBEQ",M_REL16,3}, [0x28]={"LBVC",M_REL16,3},
    [0x29]={"LBVS",M_REL16,3}, [0x2A]={"LBPL",M_REL16,3}, [0x2B]={"LBMI",M_REL16,3},
    [0x2C]={"LBGE",M_REL16,3}, [0x2D]={"LBLT",M_REL16,3}, [0x2E]={"LBGT",M_REL16,3},
    [0x2F]={"LBLE",M_REL16,3},
    [0x3F]={"SWI2",M_IMPL,2},
    [0x83]={"CMPD",M_IMM16,4}, [0x8C]={"CMPY",M_IMM16,4}, [0x8E]={"LDY", M_IMM16,4},
    [0x93]={"CMPD",M_DIR,3},   [0x9C]={"CMPY",M_DIR,3},   [0x9E]={"LDY", M_DIR,3}, [0x9F]={"STY",M_DIR,3},
    [0xA3]={"CMPD",M_IDX,3},   [0xAC]={"CMPY",M_IDX,3},   [0xAE]={"LDY", M_IDX,3}, [0xAF]={"STY",M_IDX,3},
    [0xB3]={"CMPD",M_EXT,4},   [0xBC]={"CMPY",M_EXT,4},   [0xBE]={"LDY", M_EXT,4}, [0xBF]={"STY",M_EXT,4},
    [0xCE]={"LDS", M_IMM16,4}, [0xDE]={"LDS",M_DIR,3},   [0xDF]={"STS",M_DIR,3},
    [0xEE]={"LDS", M_IDX,3},   [0xEF]={"STS",M_IDX,3},
    [0xFE]={"LDS", M_EXT,4},   [0xFF]={"STS",M_EXT,4},
    /* 6309 page 2. Register-register, W/D inherent, and W/D/Q memory. */
    [0x30]={"ADDR",M_RR,3}, [0x31]={"ADCR",M_RR,3}, [0x32]={"SUBR",M_RR,3},
    [0x33]={"SBCR",M_RR,3}, [0x34]={"ANDR",M_RR,3}, [0x35]={"ORR", M_RR,3},
    [0x36]={"EORR",M_RR,3}, [0x37]={"CMPR",M_RR,3},
    [0x38]={"PSHSW",M_IMPL,2}, [0x39]={"PULSW",M_IMPL,2},
    [0x3A]={"PSHUW",M_IMPL,2}, [0x3B]={"PULUW",M_IMPL,2},
    [0x40]={"NEGD",M_IMPL,2}, [0x43]={"COMD",M_IMPL,2}, [0x44]={"LSRD",M_IMPL,2},
    [0x46]={"RORD",M_IMPL,2}, [0x47]={"ASRD",M_IMPL,2}, [0x48]={"LSLD",M_IMPL,2},
    [0x49]={"ROLD",M_IMPL,2}, [0x4A]={"DECD",M_IMPL,2}, [0x4C]={"INCD",M_IMPL,2},
    [0x4D]={"TSTD",M_IMPL,2}, [0x4F]={"CLRD",M_IMPL,2},
    [0x53]={"COMW",M_IMPL,2}, [0x54]={"LSRW",M_IMPL,2}, [0x56]={"RORW",M_IMPL,2},
    [0x59]={"ROLW",M_IMPL,2}, [0x5A]={"DECW",M_IMPL,2}, [0x5C]={"INCW",M_IMPL,2},
    [0x5D]={"TSTW",M_IMPL,2}, [0x5F]={"CLRW",M_IMPL,2},
    [0x80]={"SUBW",M_IMM16,4}, [0x81]={"CMPW",M_IMM16,4}, [0x82]={"SBCD",M_IMM16,4},
    [0x84]={"ANDD",M_IMM16,4}, [0x85]={"BITD",M_IMM16,4}, [0x86]={"LDW", M_IMM16,4},
    [0x88]={"EORD",M_IMM16,4}, [0x89]={"ADCD",M_IMM16,4}, [0x8A]={"ORD", M_IMM16,4},
    [0x8B]={"ADDW",M_IMM16,4},
    [0x90]={"SUBW",M_DIR,3}, [0x91]={"CMPW",M_DIR,3}, [0x92]={"SBCD",M_DIR,3},
    [0x94]={"ANDD",M_DIR,3}, [0x95]={"BITD",M_DIR,3}, [0x96]={"LDW", M_DIR,3},
    [0x97]={"STW", M_DIR,3}, [0x98]={"EORD",M_DIR,3}, [0x99]={"ADCD",M_DIR,3},
    [0x9A]={"ORD", M_DIR,3}, [0x9B]={"ADDW",M_DIR,3},
    [0xA0]={"SUBW",M_IDX,3}, [0xA1]={"CMPW",M_IDX,3}, [0xA2]={"SBCD",M_IDX,3},
    [0xA4]={"ANDD",M_IDX,3}, [0xA5]={"BITD",M_IDX,3}, [0xA6]={"LDW", M_IDX,3},
    [0xA7]={"STW", M_IDX,3}, [0xA8]={"EORD",M_IDX,3}, [0xA9]={"ADCD",M_IDX,3},
    [0xAA]={"ORD", M_IDX,3}, [0xAB]={"ADDW",M_IDX,3},
    [0xB0]={"SUBW",M_EXT,4}, [0xB1]={"CMPW",M_EXT,4}, [0xB2]={"SBCD",M_EXT,4},
    [0xB4]={"ANDD",M_EXT,4}, [0xB5]={"BITD",M_EXT,4}, [0xB6]={"LDW", M_EXT,4},
    [0xB7]={"STW", M_EXT,4}, [0xB8]={"EORD",M_EXT,4}, [0xB9]={"ADCD",M_EXT,4},
    [0xBA]={"ORD", M_EXT,4}, [0xBB]={"ADDW",M_EXT,4},
    [0xDC]={"LDQ", M_DIR,3}, [0xDD]={"STQ", M_DIR,3},
    [0xEC]={"LDQ", M_IDX,3}, [0xED]={"STQ", M_IDX,3},
    [0xFC]={"LDQ", M_EXT,4}, [0xFD]={"STQ", M_EXT,4},
};

static const opinfo_t optab11[256] = {
    [0x83]={"CMPU",M_IMM16,4}, [0x8C]={"CMPS",M_IMM16,4},
    [0x93]={"CMPU",M_DIR,3},   [0x9C]={"CMPS",M_DIR,3},
    [0xA3]={"CMPU",M_IDX,3},   [0xAC]={"CMPS",M_IDX,3},
    [0xB3]={"CMPU",M_EXT,4},   [0xBC]={"CMPS",M_EXT,4},
    /* 6309 page 3. Bit ops, TFM, E/F, and the multiply/divide group. */
    [0x30]={"BAND", M_BIT,4}, [0x31]={"BIAND",M_BIT,4}, [0x32]={"BOR",  M_BIT,4},
    [0x33]={"BIOR", M_BIT,4}, [0x34]={"BEOR", M_BIT,4}, [0x35]={"BIEOR",M_BIT,4},
    [0x36]={"LDBT", M_BIT,4}, [0x37]={"STBT", M_BIT,4},
    [0x38]={"TFM", M_TFM,3}, [0x39]={"TFM", M_TFM,3},
    [0x3A]={"TFM", M_TFM,3}, [0x3B]={"TFM", M_TFM,3},
    [0x3C]={"BITMD",M_IMM8,3}, [0x3D]={"LDMD", M_IMM8,3}, [0x3F]={"SWI3", M_IMPL,2},
    [0x43]={"COME",M_IMPL,2}, [0x4A]={"DECE",M_IMPL,2}, [0x4C]={"INCE",M_IMPL,2},
    [0x4D]={"TSTE",M_IMPL,2}, [0x4F]={"CLRE",M_IMPL,2},
    [0x53]={"COMF",M_IMPL,2}, [0x5A]={"DECF",M_IMPL,2}, [0x5C]={"INCF",M_IMPL,2},
    [0x5D]={"TSTF",M_IMPL,2}, [0x5F]={"CLRF",M_IMPL,2},
    [0x80]={"SUBE",M_IMM8,3}, [0x81]={"CMPE",M_IMM8,3}, [0x86]={"LDE", M_IMM8,3},
    [0x8B]={"ADDE",M_IMM8,3}, [0x8D]={"DIVD",M_IMM8,3}, [0x8E]={"DIVQ",M_IMM16,4},
    [0x8F]={"MULD",M_IMM16,4},
    [0x90]={"SUBE",M_DIR,3}, [0x91]={"CMPE",M_DIR,3}, [0x96]={"LDE", M_DIR,3},
    [0x97]={"STE", M_DIR,3}, [0x9B]={"ADDE",M_DIR,3}, [0x9D]={"DIVD",M_DIR,3},
    [0x9E]={"DIVQ",M_DIR,3}, [0x9F]={"MULD",M_DIR,3},
    [0xA0]={"SUBE",M_IDX,3}, [0xA1]={"CMPE",M_IDX,3}, [0xA6]={"LDE", M_IDX,3},
    [0xA7]={"STE", M_IDX,3}, [0xAB]={"ADDE",M_IDX,3}, [0xAD]={"DIVD",M_IDX,3},
    [0xAE]={"DIVQ",M_IDX,3}, [0xAF]={"MULD",M_IDX,3},
    [0xB0]={"SUBE",M_EXT,4}, [0xB1]={"CMPE",M_EXT,4}, [0xB6]={"LDE", M_EXT,4},
    [0xB7]={"STE", M_EXT,4}, [0xBB]={"ADDE",M_EXT,4}, [0xBD]={"DIVD",M_EXT,4},
    [0xBE]={"DIVQ",M_EXT,4}, [0xBF]={"MULD",M_EXT,4},
    [0xC0]={"SUBF",M_IMM8,3}, [0xC1]={"CMPF",M_IMM8,3}, [0xC6]={"LDF", M_IMM8,3},
    [0xCB]={"ADDF",M_IMM8,3},
    [0xD0]={"SUBF",M_DIR,3}, [0xD1]={"CMPF",M_DIR,3}, [0xD6]={"LDF", M_DIR,3},
    [0xD7]={"STF", M_DIR,3}, [0xDB]={"ADDF",M_DIR,3},
    [0xE0]={"SUBF",M_IDX,3}, [0xE1]={"CMPF",M_IDX,3}, [0xE6]={"LDF", M_IDX,3},
    [0xE7]={"STF", M_IDX,3}, [0xEB]={"ADDF",M_IDX,3},
    [0xF0]={"SUBF",M_EXT,4}, [0xF1]={"CMPF",M_EXT,4}, [0xF6]={"LDF", M_EXT,4},
    [0xF7]={"STF", M_EXT,4}, [0xFB]={"ADDF",M_EXT,4},
};

static void fmt_idx(uint16_t *pc, char *out, size_t sz)
{
    uint8_t post = mem_read8(*pc); (*pc)++;
    int r = (post >> 5) & 3;
    const char *rs[4] = {"X","Y","U","S"};
    if (post & 0x80) {
        int indir = (post & 0x10) ? 1 : 0;
        const char *pre = indir ? "[" : "";
        const char *suf = indir ? "]" : "";
        switch (post & 0x0F) {
        case 0x00: snprintf(out, sz, "%s,%s+%s", pre, rs[r], suf); break;
        case 0x01: snprintf(out, sz, "%s,%s++%s", pre, rs[r], suf); break;
        case 0x02: snprintf(out, sz, "%s,-%s%s", pre, rs[r], suf); break;
        case 0x03: snprintf(out, sz, "%s,--%s%s", pre, rs[r], suf); break;
        case 0x04: snprintf(out, sz, "%s,%s%s", pre, rs[r], suf); break;
        case 0x05: snprintf(out, sz, "%sB,%s%s", pre, rs[r], suf); break;
        case 0x06: snprintf(out, sz, "%sA,%s%s", pre, rs[r], suf); break;
        case 0x07: snprintf(out, sz, "%sE,%s%s", pre, rs[r], suf); break;
        case 0x08: { int8_t n = (int8_t)mem_read8(*pc); (*pc)++; snprintf(out,sz,"%s%d,%s%s",pre,n,rs[r],suf); break; }
        case 0x09: { int16_t n=(int16_t)mem_read16(*pc); (*pc)+=2; snprintf(out,sz,"%s%d,%s%s",pre,n,rs[r],suf); break; }
        case 0x0A: snprintf(out, sz, "%sF,%s%s", pre, rs[r], suf); break;
        case 0x0B: snprintf(out, sz, "%sD,%s%s", pre, rs[r], suf); break;
        case 0x0C: { int8_t n=(int8_t)mem_read8(*pc); (*pc)++; snprintf(out,sz,"%s%d,PCR%s",pre,n,suf); break; }
        case 0x0D: { int16_t n=(int16_t)mem_read16(*pc); (*pc)+=2; snprintf(out,sz,"%s%d,PCR%s",pre,n,suf); break; }
        case 0x0E: snprintf(out, sz, "%sW,%s%s", pre, rs[r], suf); break;
        case 0x0F:
            if (post == 0x9F) {
                uint16_t n = mem_read16(*pc); (*pc) += 2;
                snprintf(out, sz, "[%04X]", n);
            } else snprintf(out, sz, "???");
            break;
        default: snprintf(out, sz, "???"); break;
        }
    } else {
        int off = post & 0x1F; if (off & 0x10) off |= ~0x1F;
        snprintf(out, sz, "%d,%s", off, rs[r]);
    }
}

static const char *reg6309_name(int n)
{
    static const char *names[16] = {
        "D", "X", "Y", "U", "S", "PC", "W", "V",
        "A", "B", "CC", "DP", "0", "0", "E", "F"
    };
    if (n < 0 || n > 15) return "?";
    return names[n];
}

int disasm(uint16_t addr, char *out, size_t outsz, uint16_t *next_addr)
{
    uint16_t pc = addr;
    uint8_t op = mem_read8(pc); pc++;
    uint8_t page_op = op;

    const opinfo_t *tab = optab;
    const opinfo_t *info = &tab[op];
    char operand[80] = "";

    if (op == 0x10) {
        uint8_t op2 = mem_read8(pc); pc++;
        page_op = op2;
        info = &optab10[op2];
        if (!info->mnem) info = &(opinfo_t){"???", M_IMPL, 2};
    } else if (op == 0x11) {
        uint8_t op2 = mem_read8(pc); pc++;
        page_op = op2;
        info = &optab11[op2];
        if (!info->mnem) info = &(opinfo_t){"???", M_IMPL, 2};
    }

    const char *m = info->mnem ? info->mnem : "???";

    switch (info->mode) {
    case M_IMPL: case M_INH_A: case M_INH_B: case M_INH_D: case M_SWI:
        break;
    case M_IMM8:
        snprintf(operand, sizeof(operand), "#$%02X", mem_read8(pc)); pc++;
        break;
    case M_IMM16:
        snprintf(operand, sizeof(operand), "#$%04X", mem_read16(pc)); pc += 2;
        break;
    case M_DIR: {
        uint8_t a = mem_read8(pc); pc++;
        snprintf(operand, sizeof(operand), "<$%02X", a);
        break;
    }
    case M_EXT: {
        uint16_t a = mem_read16(pc); pc += 2;
        snprintf(operand, sizeof(operand), "$%04X", a);
        break;
    }
    case M_REL8: {
        int8_t off = (int8_t)mem_read8(pc); pc++;
        uint16_t tgt = (uint16_t)(pc + off);
        snprintf(operand, sizeof(operand), "$%04X", tgt);
        break;
    }
    case M_REL16: {
        int16_t off = (int16_t)mem_read16(pc); pc += 2;
        uint16_t tgt = (uint16_t)(pc + off);
        snprintf(operand, sizeof(operand), "$%04X", tgt);
        break;
    }
    case M_IDX:
        fmt_idx(&pc, operand, sizeof(operand));
        break;
    case M_PSH: {
        uint8_t p = mem_read8(pc); pc++;
        char regs[32] = "";
        if (p & 0x80) strcat(regs, "PC,");
        if (p & 0x40) strcat(regs, "U/S,");
        if (p & 0x20) strcat(regs, "Y,");
        if (p & 0x10) strcat(regs, "X,");
        if (p & 0x08) strcat(regs, "A,");
        if (p & 0x04) strcat(regs, "B,");
        if (p & 0x02) strcat(regs, "CC,");
        if (p & 0x01) strcat(regs, "DP,");
        if (regs[0]) regs[strlen(regs)-1] = 0;
        snprintf(operand, sizeof(operand), "#$%02X  ; %s", p, regs);
        break;
    }
    case M_TFR: case M_RR: {
        uint8_t p = mem_read8(pc); pc++;
        snprintf(operand, sizeof(operand), "%s,%s",
                 reg6309_name((p >> 4) & 15), reg6309_name(p & 15));
        break;
    }
    case M_IMM32_6309:
        snprintf(operand, sizeof(operand), "#$%08X", (mem_read16(pc)<<16)|mem_read16(pc+2));
        pc += 4;
        break;
    case M_IMMDIR: {
        uint8_t imm = mem_read8(pc); pc++;
        uint8_t a = mem_read8(pc); pc++;
        snprintf(operand, sizeof(operand), "#$%02X,<$%02X", imm, a);
        break;
    }
    case M_IMMIDX: {
        uint8_t imm = mem_read8(pc); pc++;
        char idx[64];
        fmt_idx(&pc, idx, sizeof idx);
        snprintf(operand, sizeof(operand), "#$%02X,%s", imm, idx);
        break;
    }
    case M_IMMEXT: {
        uint8_t imm = mem_read8(pc); pc++;
        uint16_t a = mem_read16(pc); pc += 2;
        snprintf(operand, sizeof(operand), "#$%02X,$%04X", imm, a);
        break;
    }
    case M_TFM: {
        uint8_t p = mem_read8(pc); pc++;
        const char *s = ((p >> 4) & 15) <= 4 ? reg6309_name((p >> 4) & 15) : "?";
        const char *d = (p & 15) <= 4 ? reg6309_name(p & 15) : "?";
        static const char *form[] = {"%s+,%s+", "%s-,%s-", "%s+,%s", "%s,%s+"};
        snprintf(operand, sizeof(operand), form[page_op & 3], s, d);
        break;
    }
    case M_BIT: {
        uint8_t p = mem_read8(pc); pc++;
        uint8_t a = mem_read8(pc); pc++;
        int reg = (p >> 6) & 3;
        const char *rn = reg == 0 ? "CC" : reg == 1 ? "A" : reg == 2 ? "B" : "?";
        snprintf(operand, sizeof(operand), "%s,%d,%d,<$%02X",
                 rn, (p >> 3) & 7, p & 7, a);
        break;
    }
    default:
        break;
    }

    if (operand[0])
        snprintf(out, outsz, "%-5s %s", m, operand);
    else
        snprintf(out, outsz, "%s", m);

    if (next_addr) *next_addr = pc;
    return (int)(pc - addr);
}

void disasm_opfield(char *out, size_t outsz, uint16_t addr, int nbytes,
                    const char *mnem)
{
    int pos = 0;
    int i;
    int cols;

    if (!out || outsz == 0) return;
    out[0] = 0;
    if (!mnem) mnem = "";
    if (nbytes < 1) nbytes = 1;
    if (nbytes > 32) nbytes = 32;
    cols = nbytes > DISASM_OPCOLS ? nbytes : DISASM_OPCOLS;
    for (i = 0; i < nbytes; i++) {
        int w = snprintf(out + pos, outsz - (size_t)pos, " %02X",
                         mem_read8((uint16_t)(addr + (uint16_t)i)));
        if (w < 0 || (size_t)(pos + w) >= outsz) return;
        pos += w;
    }
    for (; i < cols; i++) {
        int w = snprintf(out + pos, outsz - (size_t)pos, "   ");
        if (w < 0 || (size_t)(pos + w) >= outsz) return;
        pos += w;
    }
    snprintf(out + pos, outsz - (size_t)pos, "    %s", mnem);
}
