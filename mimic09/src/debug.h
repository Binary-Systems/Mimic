/* debug.h — C64-style monitor / debugger for mimic09 */

#ifndef MIMIC09_DEBUG_H
#define MIMIC09_DEBUG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void monitor_run(void);           /* enters interactive monitor loop */
void monitor_step(int count);     /* single step helper */
void monitor_set_load_addr(uint16_t a);
uint16_t monitor_get_load_addr(void);

/* Programmer-model registers, in display order after PC and CC.
 * The screen editor pokes one hex digit at a time. Nibble 0 is the
 * most significant digit. */
enum {
    MON_REG_NONE = 0,
    MON_REG_PC,
    MON_REG_CC,
    MON_REG_A,
    MON_REG_B,
    MON_REG_D,
    MON_REG_E,
    MON_REG_F,
    MON_REG_W,
    MON_REG_Q,
    MON_REG_DP,
    MON_REG_X,
    MON_REG_Y,
    MON_REG_U,
    MON_REG_S,
    MON_REG_V,
    MON_REG_MD
};

struct mon_reg_cell {
    int col;
    int id;
    int nib;
};

/* Names line and ';' value line. Pass NULL to skip a buffer. */
void monitor_format_regs(char *names, size_t names_n, char *vals, size_t vals_n);
int monitor_reg_value_len(void);
int monitor_line_is_regs(const char *s, int n);
int monitor_reg_cells(struct mon_reg_cell *out, int max);
void monitor_poke_reg(int id, int nib, int digit);

#ifdef __cplusplus
}
#endif
#endif
