/* opcode6809.h - 6809 instruction table and helpers for assembler */
#ifndef OPCODE6809_H
#define OPCODE6809_H

#include <stdint.h>

typedef enum {
    AM_INH = 0,   /* inherent */
    AM_IMM,       /* immediate # */
    AM_DIR,       /* direct < */
    AM_EXT,       /* extended > */
    AM_REL,       /* short branch relative */
    AM_LREL,      /* long branch relative (16-bit) */
    AM_IX0,       /* indexed ,R */
    AM_IX1,       /* indexed n,R (5/8 bit) */
    AM_IX2,       /* indexed nn,R (16 bit) */
    AM_IXAUTO,    /* auto inc/dec */
    AM_IXIND,     /* indirect [,R] variants */
    AM_RLIST,     /* push/pull reg list */
    AM_TFR,       /* tfr/exg reg,reg */
    AM_MEM,       /* other */
    AM_INV
} amode_t;

typedef struct {
    const char *mnem;
    uint8_t     opcode;
    amode_t     mode;
    int         cycles;   /* approx base */
    int         bytes;    /* base size (may vary for indexed) */
} op_t;

const op_t *find_opcode(const char *mnem);
int is_valid_mnem(const char *mnem);

/* For a given mnemonic + addressing, return the post-byte or opcode adjustment.
   Returns <0 on failure. For indexed modes the caller still needs to encode postbyte. */
int encode_instr(const char *mnem, amode_t am, uint8_t *out_op, uint8_t *out_post, int *out_len);

#endif
