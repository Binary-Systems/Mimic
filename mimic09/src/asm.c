/* asm.c — one-line 6809/6309 assembler.
 * 6800 spellings that name the same 6809 instruction (LDAA, LDAB,
 * STAA, STAB, ORAA, ORAB) assemble to 6809 bytes.
 * 6309 mnemonics use the Hitachi opcodes, including page 2 and page 3. */

#include "asm.h"
#include "cpu.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnem;
    int inh, imm, dir, idx, ext;
    int imm16;   /* immediate is 16 bits */
    int rel;     /* branch opcode, -1 if not a branch */
    int rel16;   /* long branch */
} opc_t;

#define N -1

static const opc_t OPS[] = {
    {"NOP", 0x12, N, N, N, N, 0, N, 0},
    {"SYNC", 0x13, N, N, N, N, 0, N, 0},
    {"DAA", 0x19, N, N, N, N, 0, N, 0},
    {"SEX", 0x1D, N, N, N, N, 0, N, 0},
    {"RTS", 0x39, N, N, N, N, 0, N, 0},
    {"ABX", 0x3A, N, N, N, N, 0, N, 0},
    {"RTI", 0x3B, N, N, N, N, 0, N, 0},
    {"MUL", 0x3D, N, N, N, N, 0, N, 0},
    {"SWI", 0x3F, N, N, N, N, 0, N, 0},
    {"NEGA", 0x40, N, N, N, N, 0, N, 0},
    {"COMA", 0x43, N, N, N, N, 0, N, 0},
    {"LSRA", 0x44, N, N, N, N, 0, N, 0},
    {"RORA", 0x46, N, N, N, N, 0, N, 0},
    {"ASRA", 0x47, N, N, N, N, 0, N, 0},
    {"ASLA", 0x48, N, N, N, N, 0, N, 0},
    {"ROLA", 0x49, N, N, N, N, 0, N, 0},
    {"DECA", 0x4A, N, N, N, N, 0, N, 0},
    {"INCA", 0x4C, N, N, N, N, 0, N, 0},
    {"TSTA", 0x4D, N, N, N, N, 0, N, 0},
    {"CLRA", 0x4F, N, N, N, N, 0, N, 0},
    {"NEGB", 0x50, N, N, N, N, 0, N, 0},
    {"COMB", 0x53, N, N, N, N, 0, N, 0},
    {"LSRB", 0x54, N, N, N, N, 0, N, 0},
    {"RORB", 0x56, N, N, N, N, 0, N, 0},
    {"ASRB", 0x57, N, N, N, N, 0, N, 0},
    {"ASLB", 0x58, N, N, N, N, 0, N, 0},
    {"ROLB", 0x59, N, N, N, N, 0, N, 0},
    {"DECB", 0x5A, N, N, N, N, 0, N, 0},
    {"INCB", 0x5C, N, N, N, N, 0, N, 0},
    {"TSTB", 0x5D, N, N, N, N, 0, N, 0},
    {"CLRB", 0x5F, N, N, N, N, 0, N, 0},
    {"ORCC", N, 0x1A, N, N, N, 0, N, 0},
    {"ANDCC", N, 0x1C, N, N, N, 0, N, 0},
    {"LDA", N, 0x86, 0x96, 0xA6, 0xB6, 0, N, 0},
    {"LDB", N, 0xC6, 0xD6, 0xE6, 0xF6, 0, N, 0},
    {"LDD", N, 0xCC, 0xDC, 0xEC, 0xFC, 1, N, 0},
    {"LDX", N, 0x8E, 0x9E, 0xAE, 0xBE, 1, N, 0},
    {"STA", N, N, 0x97, 0xA7, 0xB7, 0, N, 0},
    {"STB", N, N, 0xD7, 0xE7, 0xF7, 0, N, 0},
    {"STD", N, N, 0xDD, 0xED, 0xFD, 0, N, 0},
    {"STX", N, N, 0x9F, 0xAF, 0xBF, 0, N, 0},
    {"ADDA", N, 0x8B, 0x9B, 0xAB, 0xBB, 0, N, 0},
    {"ADDB", N, 0xCB, 0xDB, 0xEB, 0xFB, 0, N, 0},
    {"ADDD", N, 0xC3, 0xD3, 0xE3, 0xF3, 1, N, 0},
    {"SUBA", N, 0x80, 0x90, 0xA0, 0xB0, 0, N, 0},
    {"SUBB", N, 0xC0, 0xD0, 0xE0, 0xF0, 0, N, 0},
    {"SUBD", N, 0x83, 0x93, 0xA3, 0xB3, 1, N, 0},
    {"CMPA", N, 0x81, 0x91, 0xA1, 0xB1, 0, N, 0},
    {"CMPB", N, 0xC1, 0xD1, 0xE1, 0xF1, 0, N, 0},
    {"CMPX", N, 0x8C, 0x9C, 0xAC, 0xBC, 1, N, 0},
    {"ANDA", N, 0x84, 0x94, 0xA4, 0xB4, 0, N, 0},
    {"ANDB", N, 0xC4, 0xD4, 0xE4, 0xF4, 0, N, 0},
    {"ORA", N, 0x8A, 0x9A, 0xAA, 0xBA, 0, N, 0},
    {"ORB", N, 0xCA, 0xDA, 0xEA, 0xFA, 0, N, 0},
    {"EORA", N, 0x88, 0x98, 0xA8, 0xB8, 0, N, 0},
    {"EORB", N, 0xC8, 0xD8, 0xE8, 0xF8, 0, N, 0},
    {"BITA", N, 0x85, 0x95, 0xA5, 0xB5, 0, N, 0},
    {"BITB", N, 0xC5, 0xD5, 0xE5, 0xF5, 0, N, 0},
    {"JMP", N, N, 0x0E, 0x6E, 0x7E, 0, N, 0},
    {"JSR", N, N, 0x9D, 0xAD, 0xBD, 0, N, 0},
    {"NEG", N, N, 0x00, 0x60, 0x70, 0, N, 0},
    {"COM", N, N, 0x03, 0x63, 0x73, 0, N, 0},
    {"LSR", N, N, 0x04, 0x64, 0x74, 0, N, 0},
    {"ROR", N, N, 0x06, 0x66, 0x76, 0, N, 0},
    {"ASR", N, N, 0x07, 0x67, 0x77, 0, N, 0},
    {"ASL", N, N, 0x08, 0x68, 0x78, 0, N, 0},
    {"LSL", N, N, 0x08, 0x68, 0x78, 0, N, 0},
    {"ROL", N, N, 0x09, 0x69, 0x79, 0, N, 0},
    {"DEC", N, N, 0x0A, 0x6A, 0x7A, 0, N, 0},
    {"INC", N, N, 0x0C, 0x6C, 0x7C, 0, N, 0},
    {"TST", N, N, 0x0D, 0x6D, 0x7D, 0, N, 0},
    {"CLR", N, N, 0x0F, 0x6F, 0x7F, 0, N, 0},
    {"BRA", N, N, N, N, N, 0, 0x20, 0},
    {"BRN", N, N, N, N, N, 0, 0x21, 0},
    {"BHI", N, N, N, N, N, 0, 0x22, 0},
    {"BLS", N, N, N, N, N, 0, 0x23, 0},
    {"BCC", N, N, N, N, N, 0, 0x24, 0},
    {"BHS", N, N, N, N, N, 0, 0x24, 0},
    {"BCS", N, N, N, N, N, 0, 0x25, 0},
    {"BLO", N, N, N, N, N, 0, 0x25, 0},
    {"BNE", N, N, N, N, N, 0, 0x26, 0},
    {"BEQ", N, N, N, N, N, 0, 0x27, 0},
    {"BVC", N, N, N, N, N, 0, 0x28, 0},
    {"BVS", N, N, N, N, N, 0, 0x29, 0},
    {"BPL", N, N, N, N, N, 0, 0x2A, 0},
    {"BMI", N, N, N, N, N, 0, 0x2B, 0},
    {"BGE", N, N, N, N, N, 0, 0x2C, 0},
    {"BLT", N, N, N, N, N, 0, 0x2D, 0},
    {"BGT", N, N, N, N, N, 0, 0x2E, 0},
    {"BLE", N, N, N, N, N, 0, 0x2F, 0},
    {"LBRA", N, N, N, N, N, 0, 0x16, 1},
    {"LBSR", N, N, N, N, N, 0, 0x17, 1},
};

static void fail(char *err, size_t n, const char *msg)
{
    if (err && n) snprintf(err, n, "%s", msg);
}

static void alias(char *mnem)
{
    if (strcmp(mnem, "LDAA") == 0) strcpy(mnem, "LDA");
    else if (strcmp(mnem, "LDAB") == 0) strcpy(mnem, "LDB");
    else if (strcmp(mnem, "STAA") == 0) strcpy(mnem, "STA");
    else if (strcmp(mnem, "STAB") == 0) strcpy(mnem, "STB");
    else if (strcmp(mnem, "ORAA") == 0) strcpy(mnem, "ORA");
    else if (strcmp(mnem, "ORAB") == 0) strcpy(mnem, "ORB");
    else if (strcmp(mnem, "LSLA") == 0) strcpy(mnem, "ASLA");
    else if (strcmp(mnem, "LSLB") == 0) strcpy(mnem, "ASLB");
    else if (strcmp(mnem, "ASLD") == 0) strcpy(mnem, "LSLD");
}

static const opc_t *find(const char *mnem)
{
    for (size_t i = 0; i < sizeof(OPS) / sizeof(OPS[0]); i++) {
        if (strcmp(OPS[i].mnem, mnem) == 0) return &OPS[i];
    }
    return NULL;
}

/* ,X /,Y /,U /,S and a numeric offset. Returns postbyte length including itself. */
static int encode_idx(const char *s, uint8_t *bytes, char *err, size_t errsz)
{
    const char *comma = strchr(s, ',');
    if (!comma || !comma[1]) {
        fail(err, errsz, "bad index");
        return -1;
    }
    char reg = (char)toupper((unsigned char)comma[1]);
    int r = reg == 'X' ? 0 : reg == 'Y' ? 1 : reg == 'U' ? 2 : reg == 'S' ? 3 : -1;
    if (r < 0) {
        fail(err, errsz, "index register");
        return -1;
    }
    while (isspace((unsigned char)*s)) s++;
    if (s == comma) {
        bytes[0] = (uint8_t)(0x84 | (r << 5));
        return 1;
    }
    {
        const char *le = comma;
        while (le > s && isspace((unsigned char)le[-1])) le--;
        if (le - s == 1) {
            char c = (char)toupper((unsigned char)*s);
            int base = -1;
            if (c == 'A') base = 0x86;
            else if (c == 'B') base = 0x85;
            else if (c == 'D') base = 0x8B;
            else if (c == 'E') base = 0x87;
            else if (c == 'F') base = 0x8A;
            else if (c == 'W') base = 0x8E;
            if (base >= 0) {
                bytes[0] = (uint8_t)(base | (r << 5));
                return 1;
            }
        }
    }
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    unsigned long uv = 0;
    const char *end = NULL;
    if (!parse_radix(s, &uv, &end)) {
        fail(err, errsz, "bad offset");
        return -1;
    }
    while (*end && isspace((unsigned char)*end)) end++;
    if (end != comma) {
        fail(err, errsz, "bad offset");
        return -1;
    }
    long off = neg ? -(long)uv : (long)uv;
    if (off >= -16 && off <= 15) {
        bytes[0] = (uint8_t)((r << 5) | (off & 0x1F));
        return 1;
    }
    if (off >= -128 && off <= 127) {
        bytes[0] = (uint8_t)(0x88 | (r << 5));
        bytes[1] = (uint8_t)off;
        return 2;
    }
    if (off >= -32768 && off <= 65535) {
        uint16_t w = (uint16_t)off;
        bytes[0] = (uint8_t)(0x89 | (r << 5));
        bytes[1] = (uint8_t)(w >> 8);
        bytes[2] = (uint8_t)w;
        return 3;
    }
    fail(err, errsz, "offset range");
    return -1;
}

/* Hitachi 6309. pre/inh/dir/idx/ext describe memory and inherent forms.
 * imm_pre/imm/wide describe immediate. kind selects a special operand. */
enum { K_OP = 0, K_IMMEA = 1, K_RR = 2, K_TFM = 3, K_BIT = 4 };

typedef struct {
    const char *mnem;
    int pre;
    int inh, dir, idx, ext;
    int imm_pre, imm, wide;
    int kind;
} op6309_t;

static const op6309_t OPS6309[] = {
    {"SEXW", 0, 0x14, N, N, N, N, N, 0, K_OP},
    {"LDQ", 0x10, N, 0xDC, 0xEC, 0xFC, 0, 0xCD, 2, K_OP},
    {"STQ", 0x10, N, 0xDD, 0xED, 0xFD, N, N, 0, K_OP},
    {"OIM", 0, N, 0x01, 0x61, 0x71, N, N, 0, K_IMMEA},
    {"AIM", 0, N, 0x02, 0x62, 0x72, N, N, 0, K_IMMEA},
    {"EIM", 0, N, 0x05, 0x65, 0x75, N, N, 0, K_IMMEA},
    {"TIM", 0, N, 0x0B, 0x6B, 0x7B, N, N, 0, K_IMMEA},
    {"ADDR", 0x10, 0x30, N, N, N, N, N, 0, K_RR},
    {"ADCR", 0x10, 0x31, N, N, N, N, N, 0, K_RR},
    {"SUBR", 0x10, 0x32, N, N, N, N, N, 0, K_RR},
    {"SBCR", 0x10, 0x33, N, N, N, N, N, 0, K_RR},
    {"ANDR", 0x10, 0x34, N, N, N, N, N, 0, K_RR},
    {"ORR",  0x10, 0x35, N, N, N, N, N, 0, K_RR},
    {"EORR", 0x10, 0x36, N, N, N, N, N, 0, K_RR},
    {"CMPR", 0x10, 0x37, N, N, N, N, N, 0, K_RR},
    {"TFR", 0, 0x1F, N, N, N, N, N, 0, K_RR},
    {"EXG", 0, 0x1E, N, N, N, N, N, 0, K_RR},
    {"PSHSW", 0x10, 0x38, N, N, N, N, N, 0, K_OP},
    {"PULSW", 0x10, 0x39, N, N, N, N, N, 0, K_OP},
    {"PSHUW", 0x10, 0x3A, N, N, N, N, N, 0, K_OP},
    {"PULUW", 0x10, 0x3B, N, N, N, N, N, 0, K_OP},
    {"NEGD", 0x10, 0x40, N, N, N, N, N, 0, K_OP},
    {"COMD", 0x10, 0x43, N, N, N, N, N, 0, K_OP},
    {"LSRD", 0x10, 0x44, N, N, N, N, N, 0, K_OP},
    {"RORD", 0x10, 0x46, N, N, N, N, N, 0, K_OP},
    {"ASRD", 0x10, 0x47, N, N, N, N, N, 0, K_OP},
    {"LSLD", 0x10, 0x48, N, N, N, N, N, 0, K_OP},
    {"ROLD", 0x10, 0x49, N, N, N, N, N, 0, K_OP},
    {"DECD", 0x10, 0x4A, N, N, N, N, N, 0, K_OP},
    {"INCD", 0x10, 0x4C, N, N, N, N, N, 0, K_OP},
    {"TSTD", 0x10, 0x4D, N, N, N, N, N, 0, K_OP},
    {"CLRD", 0x10, 0x4F, N, N, N, N, N, 0, K_OP},
    {"COMW", 0x10, 0x53, N, N, N, N, N, 0, K_OP},
    {"LSRW", 0x10, 0x54, N, N, N, N, N, 0, K_OP},
    {"RORW", 0x10, 0x56, N, N, N, N, N, 0, K_OP},
    {"ROLW", 0x10, 0x59, N, N, N, N, N, 0, K_OP},
    {"DECW", 0x10, 0x5A, N, N, N, N, N, 0, K_OP},
    {"INCW", 0x10, 0x5C, N, N, N, N, N, 0, K_OP},
    {"TSTW", 0x10, 0x5D, N, N, N, N, N, 0, K_OP},
    {"CLRW", 0x10, 0x5F, N, N, N, N, N, 0, K_OP},
    {"SUBW", 0x10, N, 0x90, 0xA0, 0xB0, 0x10, 0x80, 1, K_OP},
    {"CMPW", 0x10, N, 0x91, 0xA1, 0xB1, 0x10, 0x81, 1, K_OP},
    {"SBCD", 0x10, N, 0x92, 0xA2, 0xB2, 0x10, 0x82, 1, K_OP},
    {"ANDD", 0x10, N, 0x94, 0xA4, 0xB4, 0x10, 0x84, 1, K_OP},
    {"BITD", 0x10, N, 0x95, 0xA5, 0xB5, 0x10, 0x85, 1, K_OP},
    {"LDW",  0x10, N, 0x96, 0xA6, 0xB6, 0x10, 0x86, 1, K_OP},
    {"STW",  0x10, N, 0x97, 0xA7, 0xB7, N, N, 0, K_OP},
    {"EORD", 0x10, N, 0x98, 0xA8, 0xB8, 0x10, 0x88, 1, K_OP},
    {"ADCD", 0x10, N, 0x99, 0xA9, 0xB9, 0x10, 0x89, 1, K_OP},
    {"ORD",  0x10, N, 0x9A, 0xAA, 0xBA, 0x10, 0x8A, 1, K_OP},
    {"ADDW", 0x10, N, 0x9B, 0xAB, 0xBB, 0x10, 0x8B, 1, K_OP},
    {"BAND",  0x11, 0x30, N, N, N, N, N, 0, K_BIT},
    {"BIAND", 0x11, 0x31, N, N, N, N, N, 0, K_BIT},
    {"BOR",   0x11, 0x32, N, N, N, N, N, 0, K_BIT},
    {"BIOR",  0x11, 0x33, N, N, N, N, N, 0, K_BIT},
    {"BEOR",  0x11, 0x34, N, N, N, N, N, 0, K_BIT},
    {"BIEOR", 0x11, 0x35, N, N, N, N, N, 0, K_BIT},
    {"LDBT",  0x11, 0x36, N, N, N, N, N, 0, K_BIT},
    {"STBT",  0x11, 0x37, N, N, N, N, N, 0, K_BIT},
    {"TFM", 0x11, N, N, N, N, N, N, 0, K_TFM},
    {"BITMD", 0, N, N, N, N, 0x11, 0x3C, 0, K_OP},
    {"LDMD",  0, N, N, N, N, 0x11, 0x3D, 0, K_OP},
    {"SWI3", 0x11, 0x3F, N, N, N, N, N, 0, K_OP},
    {"COME", 0x11, 0x43, N, N, N, N, N, 0, K_OP},
    {"DECE", 0x11, 0x4A, N, N, N, N, N, 0, K_OP},
    {"INCE", 0x11, 0x4C, N, N, N, N, N, 0, K_OP},
    {"TSTE", 0x11, 0x4D, N, N, N, N, N, 0, K_OP},
    {"CLRE", 0x11, 0x4F, N, N, N, N, N, 0, K_OP},
    {"COMF", 0x11, 0x53, N, N, N, N, N, 0, K_OP},
    {"DECF", 0x11, 0x5A, N, N, N, N, N, 0, K_OP},
    {"INCF", 0x11, 0x5C, N, N, N, N, N, 0, K_OP},
    {"TSTF", 0x11, 0x5D, N, N, N, N, N, 0, K_OP},
    {"CLRF", 0x11, 0x5F, N, N, N, N, N, 0, K_OP},
    {"SUBE", 0x11, N, 0x90, 0xA0, 0xB0, 0x11, 0x80, 0, K_OP},
    {"CMPE", 0x11, N, 0x91, 0xA1, 0xB1, 0x11, 0x81, 0, K_OP},
    {"LDE",  0x11, N, 0x96, 0xA6, 0xB6, 0x11, 0x86, 0, K_OP},
    {"STE",  0x11, N, 0x97, 0xA7, 0xB7, N, N, 0, K_OP},
    {"ADDE", 0x11, N, 0x9B, 0xAB, 0xBB, 0x11, 0x8B, 0, K_OP},
    {"DIVD", 0x11, N, 0x9D, 0xAD, 0xBD, 0x11, 0x8D, 0, K_OP},
    {"DIVQ", 0x11, N, 0x9E, 0xAE, 0xBE, 0x11, 0x8E, 1, K_OP},
    {"MULD", 0x11, N, 0x9F, 0xAF, 0xBF, 0x11, 0x8F, 1, K_OP},
    {"SUBF", 0x11, N, 0xD0, 0xE0, 0xF0, 0x11, 0xC0, 0, K_OP},
    {"CMPF", 0x11, N, 0xD1, 0xE1, 0xF1, 0x11, 0xC1, 0, K_OP},
    {"LDF",  0x11, N, 0xD6, 0xE6, 0xF6, 0x11, 0xC6, 0, K_OP},
    {"STF",  0x11, N, 0xD7, 0xE7, 0xF7, N, N, 0, K_OP},
    {"ADDF", 0x11, N, 0xDB, 0xEB, 0xFB, 0x11, 0xCB, 0, K_OP},
};

static const op6309_t *find6309(const char *mnem)
{
    for (size_t i = 0; i < sizeof(OPS6309) / sizeof(OPS6309[0]); i++) {
        if (strcmp(OPS6309[i].mnem, mnem) == 0) return &OPS6309[i];
    }
    return NULL;
}

static const char *skip_sp(const char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Longest names first so PC and CC win over a single letter. */
static int match_reg(const char *s, const char **end)
{
    static const char *names[] = {
        "PC", "CC", "DP", "D", "X", "Y", "U", "S", "W", "V",
        "A", "B", "E", "F", "0"
    };
    static const int codes[] = {
        5, 10, 11, 0, 1, 2, 3, 4, 6, 7, 8, 9, 14, 15, 12
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t n = strlen(names[i]);
        size_t k;
        for (k = 0; k < n; k++) {
            if (toupper((unsigned char)s[k]) != names[i][k]) break;
        }
        if (k != n) continue;
        if (isalnum((unsigned char)s[n])) continue;
        if (end) *end = s + n;
        return codes[i];
    }
    return -1;
}

static int commit_bytes(uint16_t addr, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) mem_write8((uint16_t)(addr + i), b[i]);
    return n;
}

static int put_op(uint8_t *b, int n, int pre, int opc)
{
    if (pre) b[n++] = (uint8_t)pre;
    b[n++] = (uint8_t)opc;
    return n;
}

/* Address bytes for a 6309 memory operand. *opc receives dir, idx, or ext. */
static int ea_bytes(const op6309_t *op, const char *p, uint8_t *extra, int *opc,
                    char *err, size_t errsz)
{
    p = skip_sp(p);
    if (strchr(p, ',')) {
        if (op->idx < 0) {
            fail(err, errsz, "no indexed form");
            return -1;
        }
        int n = encode_idx(p, extra, err, errsz);
        if (n < 0) return -1;
        *opc = op->idx;
        return n;
    }
    int force_dir = 0, force_ext = 0;
    if (*p == '<') { force_dir = 1; p++; }
    else if (*p == '>') { force_ext = 1; p++; }
    unsigned long v = 0;
    const char *end = NULL;
    if (!parse_radix(p, &v, &end)) {
        fail(err, errsz, "bad operand");
        return -1;
    }
    /* A short address uses direct when that form exists. `<` forces it.
     * `>` falls through to extended. Bit ops publish direct only. */
    if (force_dir || (!force_ext && op->dir >= 0 && v <= 0xFF)) {
        if (op->dir < 0 || v > 0xFF) {
            fail(err, errsz, "not direct");
            return -1;
        }
        extra[0] = (uint8_t)v;
        *opc = op->dir;
        return 1;
    }
    if (op->ext < 0 || force_dir) {
        fail(err, errsz, "not extended");
        return -1;
    }
    extra[0] = (uint8_t)(v >> 8);
    extra[1] = (uint8_t)v;
    *opc = op->ext;
    return 2;
}

/* Assemble one 6309 instruction. -2 means this mnemonic is not 6309. */
static int asm_6309(uint16_t addr, const char *mnem, const char *p,
                    char *err, size_t errsz)
{
    const op6309_t *op = find6309(mnem);
    uint8_t b[12];
    int n = 0;
    if (!op) return -2;
    p = skip_sp(p);

    if (op->kind == K_RR) {
        const char *end = NULL;
        int src, dst;
        if (!*p) {
            fail(err, errsz, "operand required");
            return -1;
        }
        src = match_reg(p, &end);
        if (src < 0) {
            fail(err, errsz, "register");
            return -1;
        }
        end = skip_sp(end);
        if (*end != ',') {
            fail(err, errsz, "register pair");
            return -1;
        }
        dst = match_reg(skip_sp(end + 1), &end);
        if (dst < 0) {
            fail(err, errsz, "register");
            return -1;
        }
        n = put_op(b, 0, op->pre, op->inh);
        b[n++] = (uint8_t)((src << 4) | dst);
        return commit_bytes(addr, b, n);
    }

    if (op->kind == K_TFM) {
        const char *end = NULL;
        int src, dst, sp = 0, dp = 0, opc;
        src = match_reg(p, &end);
        if (src < 0 || src > 4) {
            fail(err, errsz, "tfm register");
            return -1;
        }
        end = skip_sp(end);
        if (*end == '+') { sp = 1; end++; }
        else if (*end == '-') { sp = -1; end++; }
        end = skip_sp(end);
        if (*end != ',') {
            fail(err, errsz, "tfm pair");
            return -1;
        }
        dst = match_reg(skip_sp(end + 1), &end);
        if (dst < 0 || dst > 4) {
            fail(err, errsz, "tfm register");
            return -1;
        }
        end = skip_sp(end);
        if (*end == '+') { dp = 1; end++; }
        else if (*end == '-') { dp = -1; end++; }
        if (sp == 1 && dp == 1) opc = 0x38;
        else if (sp == -1 && dp == -1) opc = 0x39;
        else if (sp == 1 && dp == 0) opc = 0x3A;
        else if (sp == 0 && dp == 1) opc = 0x3B;
        else {
            fail(err, errsz, "tfm mode");
            return -1;
        }
        n = put_op(b, 0, op->pre, opc);
        b[n++] = (uint8_t)((src << 4) | dst);
        return commit_bytes(addr, b, n);
    }

    if (op->kind == K_BIT) {
        const char *end = NULL;
        int reg = match_reg(p, &end);
        int field = reg == 10 ? 0 : reg == 8 ? 1 : reg == 9 ? 2 : -1;
        unsigned long sbit = 0, dbit = 0;
        uint8_t ea[2];
        int opc = 0;
        int en;
        op6309_t direct_only;
        if (field < 0) {
            fail(err, errsz, "bit register");
            return -1;
        }
        end = skip_sp(end);
        if (*end != ',') {
            fail(err, errsz, "bit operand");
            return -1;
        }
        if (!parse_radix(skip_sp(end + 1), &sbit, &end) || sbit > 7) {
            fail(err, errsz, "source bit");
            return -1;
        }
        end = skip_sp(end);
        if (*end != ',') {
            fail(err, errsz, "bit operand");
            return -1;
        }
        if (!parse_radix(skip_sp(end + 1), &dbit, &end) || dbit > 7) {
            fail(err, errsz, "dest bit");
            return -1;
        }
        end = skip_sp(end);
        if (*end != ',') {
            fail(err, errsz, "bit operand");
            return -1;
        }
        direct_only = *op;
        direct_only.dir = 0;
        direct_only.idx = N;
        direct_only.ext = N;
        en = ea_bytes(&direct_only, end + 1, ea, &opc, err, errsz);
        if (en != 1) {
            if (en >= 0) fail(err, errsz, "not direct");
            return -1;
        }
        (void)opc;
        n = put_op(b, 0, op->pre, op->inh);
        b[n++] = (uint8_t)((field << 6) | ((sbit & 7) << 3) | (dbit & 7));
        b[n++] = ea[0];
        return commit_bytes(addr, b, n);
    }

    if (op->kind == K_IMMEA) {
        unsigned long imm = 0;
        const char *end = NULL;
        uint8_t extra[4];
        int opc = 0;
        int en;
        if (*p != '#') {
            fail(err, errsz, "immediate required");
            return -1;
        }
        if (!parse_radix(p + 1, &imm, &end) || imm > 0xFF) {
            fail(err, errsz, "bad immediate");
            return -1;
        }
        end = skip_sp(end);
        if (*end != ',') {
            fail(err, errsz, "address required");
            return -1;
        }
        en = ea_bytes(op, end + 1, extra, &opc, err, errsz);
        if (en < 0) return -1;
        n = put_op(b, 0, op->pre, opc);
        b[n++] = (uint8_t)imm;
        for (int i = 0; i < en; i++) b[n++] = extra[i];
        return commit_bytes(addr, b, n);
    }

    if (!*p) {
        if (op->inh < 0) {
            fail(err, errsz, "operand required");
            return -1;
        }
        n = put_op(b, 0, op->pre, op->inh);
        return commit_bytes(addr, b, n);
    }

    if (*p == '#') {
        unsigned long v = 0;
        const char *end = NULL;
        if (op->imm < 0) {
            fail(err, errsz, "no immediate");
            return -1;
        }
        if (!parse_radix(p + 1, &v, &end)) {
            fail(err, errsz, "bad immediate");
            return -1;
        }
        if (op->wide == 0 && v > 0xFF) {
            fail(err, errsz, "immediate too wide");
            return -1;
        }
        if (op->wide == 1 && v > 0xFFFF) {
            fail(err, errsz, "immediate too wide");
            return -1;
        }
        if (op->wide == 2 && v > 0xFFFFFFFFUL) {
            fail(err, errsz, "immediate too wide");
            return -1;
        }
        n = put_op(b, 0, op->imm_pre, op->imm);
        if (op->wide == 2) {
            b[n++] = (uint8_t)(v >> 24);
            b[n++] = (uint8_t)(v >> 16);
            b[n++] = (uint8_t)(v >> 8);
            b[n++] = (uint8_t)v;
        } else if (op->wide == 1) {
            b[n++] = (uint8_t)(v >> 8);
            b[n++] = (uint8_t)v;
        } else {
            b[n++] = (uint8_t)v;
        }
        return commit_bytes(addr, b, n);
    }

    {
        uint8_t extra[4];
        int opc = 0;
        int en = ea_bytes(op, p, extra, &opc, err, errsz);
        if (en < 0) return -1;
        n = put_op(b, 0, op->pre, opc);
        for (int i = 0; i < en; i++) b[n++] = extra[i];
        return commit_bytes(addr, b, n);
    }
}

int asm_instr(uint16_t addr, const char *text, char *err, size_t errsz)
{
    char buf[160];
    if (!text) {
        fail(err, errsz, "no text");
        return -1;
    }
    while (*text && isspace((unsigned char)*text)) text++;
    snprintf(buf, sizeof(buf), "%s", text);
    char *nl = strpbrk(buf, "\r\n");
    if (nl) *nl = 0;

    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) {
        fail(err, errsz, "empty");
        return -1;
    }
    char mnem[12];
    size_t mi = 0;
    while (*p && !isspace((unsigned char)*p) && mi + 1 < sizeof(mnem)) {
        mnem[mi++] = (char)toupper((unsigned char)*p++);
    }
    mnem[mi] = 0;
    while (*p && isspace((unsigned char)*p)) p++;
    alias(mnem);
    const opc_t *op = find(mnem);
    if (!op) {
        int n6309 = asm_6309(addr, mnem, p, err, errsz);
        if (n6309 != -2) return n6309;
        fail(err, errsz, "unknown mnemonic");
        return -1;
    }

    if (!*p) {
        if (op->inh < 0) {
            fail(err, errsz, "operand required");
            return -1;
        }
        mem_write8(addr, (uint8_t)op->inh);
        return 1;
    }

    if (op->rel >= 0) {
        unsigned long target = 0;
        const char *end = NULL;
        if (!parse_radix(p, &target, &end)) {
            fail(err, errsz, "bad branch target");
            return -1;
        }
        int len = op->rel16 ? 3 : 2;
        int rel = (int)target - (int)(addr + (uint16_t)len);
        if (!op->rel16 && (rel < -128 || rel > 127)) {
            fail(err, errsz, "branch out of range");
            return -1;
        }
        mem_write8(addr, (uint8_t)op->rel);
        if (op->rel16) mem_write16((uint16_t)(addr + 1), (uint16_t)rel);
        else mem_write8((uint16_t)(addr + 1), (uint8_t)rel);
        return len;
    }

    if (*p == '#') {
        if (op->imm < 0) {
            fail(err, errsz, "no immediate");
            return -1;
        }
        unsigned long v = 0;
        const char *end = NULL;
        if (!parse_radix(p + 1, &v, &end)) {
            fail(err, errsz, "bad immediate");
            return -1;
        }
        if (!op->imm16 && v > 0xFF) {
            fail(err, errsz, "immediate too wide");
            return -1;
        }
        mem_write8(addr, (uint8_t)op->imm);
        if (op->imm16) {
            mem_write16((uint16_t)(addr + 1), (uint16_t)v);
            return 3;
        }
        mem_write8((uint16_t)(addr + 1), (uint8_t)v);
        return 2;
    }

    if (strchr(p, ',')) {
        if (op->idx < 0) {
            fail(err, errsz, "no indexed form");
            return -1;
        }
        uint8_t extra[3];
        int n = encode_idx(p, extra, err, errsz);
        if (n < 0) return -1;
        mem_write8(addr, (uint8_t)op->idx);
        for (int i = 0; i < n; i++) mem_write8((uint16_t)(addr + 1 + i), extra[i]);
        return 1 + n;
    }

    int force_dir = 0, force_ext = 0;
    if (*p == '<') { force_dir = 1; p++; }
    else if (*p == '>') { force_ext = 1; p++; }
    unsigned long v = 0;
    const char *end = NULL;
    if (!parse_radix(p, &v, &end)) {
        fail(err, errsz, "bad operand");
        return -1;
    }
    int direct = force_dir || (!force_ext && v <= 0xFF && op->dir >= 0 && op->ext >= 0);
    if (force_dir || (direct && !force_ext && op->dir >= 0 && (v <= 0xFF))) {
        if (op->dir < 0 || v > 0xFF) {
            fail(err, errsz, "not direct");
            return -1;
        }
        mem_write8(addr, (uint8_t)op->dir);
        mem_write8((uint16_t)(addr + 1), (uint8_t)v);
        return 2;
    }
    if (op->ext < 0) {
        fail(err, errsz, "not extended");
        return -1;
    }
    mem_write8(addr, (uint8_t)op->ext);
    mem_write16((uint16_t)(addr + 1), (uint16_t)v);
    return 3;
}
