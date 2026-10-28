/* cpu.h — mimic09 6809/6309 CPU core
 *
 * Extremely efficient interpreter core for macOS / UNIX.
 * All memory traffic from the emulated CPU goes through the accessors
 * so devices (ACIA/PIA) and debug watchpoints/breakpoints always work.
 */

#ifndef MIMIC09_CPU_H
#define MIMIC09_CPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Condition Code bits (6809/6309) */
#define CC_C 0x01
#define CC_V 0x02
#define CC_Z 0x04
#define CC_N 0x08
#define CC_I 0x10
#define CC_H 0x20
#define CC_F 0x40
#define CC_E 0x80

/* CPU model */
typedef enum {
    CPU_6809 = 0,
    CPU_6309 = 1
} cpu_model_t;

/* Primary CPU state */
typedef struct cpu_t {
    /* 16-bit registers */
    uint16_t pc;   /* Program Counter */
    uint16_t x, y, u, s;

    /* Accumulators */
    union {
        uint16_t d;        /* D = A:B (A high byte) */
        struct {
            uint8_t b;     /* low */
            uint8_t a;     /* high */
        } ab;
    } d;

    /* 6309 extras */
    uint8_t  e, f;         /* accumulators E, F */
    uint16_t v;            /* V register (6309) */
    uint8_t  md;           /* Mode register (6309) */
    /* Q is 32-bit: conceptually A:B:E:F or D:W. We keep a shadow for speed. */
    uint32_t q;

    /* Condition codes (full byte: EFHINZVC) */
    uint8_t cc;

    /* CPU type and internal state */
    cpu_model_t model;
    bool        native6309;   /* true when 6309 native mode (MD bit) */

    /* Execution control */
    bool        halted;
    uint64_t    total_cycles; /* cumulative cycle count (best effort) */

    /* Break / debug interface (set by monitor) */
    bool        debug_mode;   /* when true, step does lighter work + checks */
    bool        stop_requested;
} cpu_t;

/* Global instance (for speed in hot paths; one emulator for now) */
extern cpu_t cpu;

/* Flat 64KB memory. Device and debug layers sit in the accessors. */
extern uint8_t memory[65536];

/* Memory accessors — these are the *only* way the CPU should touch memory.
 * They handle breakpoints, watchpoints, ACIA, PIA, etc.
 */
uint8_t  cpu_read8(uint16_t addr);
void     cpu_write8(uint16_t addr, uint8_t val);
uint16_t cpu_read16(uint16_t addr);   /* big-endian (6809 order) */
void     cpu_write16(uint16_t addr, uint16_t val);

/* Fast direct RAM access for monitor / loader / bulk ops (bypasses devices) */
static inline uint8_t  mem_read8(uint16_t a)  { return memory[a]; }
static inline void     mem_write8(uint16_t a, uint8_t v) { memory[a] = v; }
static inline uint16_t mem_read16(uint16_t a) { return (memory[a] << 8) | memory[(uint16_t)(a+1)]; }
static inline void     mem_write16(uint16_t a, uint16_t v) {
    memory[a] = v >> 8; memory[(uint16_t)(a+1)] = v & 0xFF;
}

/* CPU lifecycle */
void cpu_init(cpu_model_t model);
void cpu_reset(void);                 /* loads reset vector from FFFE:FFFF */

/* Single instruction step. Returns approximate cycles consumed by the instr. */
int  cpu_step(void);

/* Run until stop condition (breakpoint, stop_requested, or halted).
 * In debug mode this will honor single-step and breakpoints aggressively.
 * Returns reason code (0 = normal stop, >0 for breakpoint id or other).
 */
int  cpu_run(void);

/* Breakpoint / watchpoint interface (simple for v1; monitor owns the lists) */
void bp_set_exec(uint16_t addr);
void bp_clear_exec(uint16_t addr);
void bp_clear_all(void);
bool bp_check_exec(uint16_t addr);    /* called by cpu_step before exec */

/* Optional memory access watch (read or write). For speed we can OR flags. */
#define BP_READ  1
#define BP_WRITE 2
void bp_set_watch(uint16_t addr, int flags);
void bp_clear_watch(uint16_t addr);

/* Interrupt requests (level sensitive until serviced) */
void cpu_assert_irq(void);   /* maskable IRQ */
void cpu_assert_firq(void);  /* fast IRQ (6309/6809) */
void cpu_assert_nmi(void);

/* Utility */
void cpu_dump_regs(char *buf, size_t buflen);  /* for monitor R command */
void cpu_set_model(cpu_model_t m);
uint8_t cpu_get_dp(void);
void cpu_set_dp(uint8_t v);

/* which: 0=E, 1=F, 2=V, 3=MD.
 * A write reveals that register. In emulation mode an unwritten one is
 * shown as dashes and still reads as zero. Native 6309 mode reveals all four. */
void cpu_write_e(uint8_t v);
void cpu_write_f(uint8_t v);
void cpu_write_v(uint16_t v);
void cpu_write_md(uint8_t v);
void cpu_hide_6309(int which);
int cpu_6309_visible(int which);

/* Device registration (called by acia/pia init) */
void cpu_register_device(uint16_t base,
                         uint8_t (*readfn)(uint16_t),
                         void    (*writefn)(uint16_t, uint8_t));

/* CC helpers (inlined for speed) */
static inline void cc_set(uint8_t mask) { cpu.cc |= mask; }
static inline void cc_clear(uint8_t mask) { cpu.cc &= ~mask; }
static inline bool cc_test(uint8_t mask) { return (cpu.cc & mask) != 0; }

static inline void set_nz8(uint8_t v) {
    cc_clear(CC_N | CC_Z);
    if (v & 0x80) cc_set(CC_N);
    if (v == 0)   cc_set(CC_Z);
}
static inline void set_nz16(uint16_t v) {
    cc_clear(CC_N | CC_Z);
    if (v & 0x8000) cc_set(CC_N);
    if (v == 0)     cc_set(CC_Z);
}

#ifdef __cplusplus
}
#endif
#endif /* MIMIC09_CPU_H */
