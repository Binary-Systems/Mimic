/* debug.c — Mimic SuperMon monitor for mimic09.
 * Command letters match SuperMon+64 V1.2. Help is ?. H is hunt.
 * S with a count is the Mimic step command. S with a path saves.
 * B is the Mimic breakpoint command. ! is the Mimic shell command.
 * : pokes bytes. Q exits, same as X. */

#include "debug.h"
#include "cpu.h"
#include "disasm.h"
#include "asm.h"
#include "loader.h"
#include "acia.h"
#include "screen.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>

/* Monitor text goes through here so a terminal session can keep the lines
 * it drew. A pipe still writes to stdout and the scripted tests stay put. */
static void mon_write(const char *s, size_t n)
{
    if (n == 0) return;
    if (screen_is_active()) screen_feed(s, n);
    else {
        fwrite(s, 1, n, stdout);
        fflush(stdout);
    }
}

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static void mon_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    mon_write(buf, (size_t)n);
}

static void mon_puts(const char *s)
{
    mon_write(s, strlen(s));
    mon_write("\n", 1);
}

static void mon_putchar(int c)
{
    char ch = (char)c;
    mon_write(&ch, 1);
}

#define printf mon_printf
#define puts mon_puts
#define putchar mon_putchar

static uint16_t next_dasm = 0;
static uint16_t next_mem  = 0;
static uint16_t next_asm  = 0;
static uint16_t load_addr = 0x0000;

void monitor_set_load_addr(uint16_t a) { load_addr = a; }
uint16_t monitor_get_load_addr(void)   { return load_addr; }

static volatile int break_flag = 0;
static void sigint_handler(int sig) { (void)sig; break_flag = 1; }

static struct { uint16_t addr; int used; } breakpoints[32];

static char command_char(char *line)
{
    char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p && isalpha((unsigned char)*p)) {
        *p = (char)toupper((unsigned char)*p);
        return *p;
    }
    return *p;
}

static int parse_num(const char *s, unsigned long *out, const char **end)
{
    return parse_radix(s, out, end);
}

static int parse_u16(const char *s, uint16_t *out, const char **end)
{
    unsigned long v = 0;
    const char *e = NULL;
    if (!parse_num(s, &v, &e)) return 0;
    *out = (uint16_t)v;
    if (end) *end = e;
    return 1;
}

static void skip_ws(char **p)
{
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static int next_u16(char **p, uint16_t *out)
{
    skip_ws(p);
    const char *e = NULL;
    if (!parse_u16(*p, out, &e)) return 0;
    *p = (char *)e;
    return 1;
}

/* Two lines, SuperMon style: names, then one ';' value line.
 * PC, CC, the flag letters, A, B, E, F, DP, X, Y, U, S, V, MD.
 * D, W, and Q stay settable with ';' but are not printed: D is A:B and
 * W is E:F. In emulation mode E, F, V, and MD are dashes until written.
 * Native 6309 mode prints their values. A dash reads as zero. */
enum { REG_FLAGS = 1 };

static const struct {
    const char *name;
    int width;
    int id;
    int flags;
} reg_fields[] = {
    {"PC", 4, MON_REG_PC, 0},
    {"CC", 2, MON_REG_CC, 0},
    {"EFHINZVC", 8, MON_REG_NONE, REG_FLAGS},
    {"A", 2, MON_REG_A, 0},
    {"B", 2, MON_REG_B, 0},
    {"E", 2, MON_REG_E, 0},
    {"F", 2, MON_REG_F, 0},
    {"DP", 2, MON_REG_DP, 0},
    {"X", 4, MON_REG_X, 0},
    {"Y", 4, MON_REG_Y, 0},
    {"U", 4, MON_REG_U, 0},
    {"S", 4, MON_REG_S, 0},
    {"V", 4, MON_REG_V, 0},
    {"MD", 2, MON_REG_MD, 0},
};

static int reg_nfields(void)
{
    return (int)(sizeof reg_fields / sizeof reg_fields[0]);
}

static void format_flags(char out[8])
{
    static const char *names = "EFHINZVC";
    static const uint8_t bits[8] = {
        CC_E, CC_F, CC_H, CC_I, CC_N, CC_Z, CC_V, CC_C
    };
    int i;
    for (i = 0; i < 8; i++) out[i] = (cpu.cc & bits[i]) ? names[i] : '.';
}

static int latent_which(int id)
{
    if (id == MON_REG_E) return 0;
    if (id == MON_REG_F) return 1;
    if (id == MON_REG_V) return 2;
    if (id == MON_REG_MD) return 3;
    return -1;
}

static int field_latent(int id)
{
    int which = latent_which(id);
    if (which < 0) return 0;
    return !cpu_6309_visible(which);
}

static uint32_t reg_get(int id)
{
    switch (id) {
    case MON_REG_PC: return cpu.pc;
    case MON_REG_CC: return cpu.cc;
    case MON_REG_A: return cpu.d.ab.a;
    case MON_REG_B: return cpu.d.ab.b;
    case MON_REG_D: return cpu.d.d;
    case MON_REG_E: return cpu.e;
    case MON_REG_F: return cpu.f;
    case MON_REG_W: return ((uint32_t)cpu.e << 8) | cpu.f;
    case MON_REG_Q: return cpu.q;
    case MON_REG_DP: return cpu_get_dp();
    case MON_REG_X: return cpu.x;
    case MON_REG_Y: return cpu.y;
    case MON_REG_U: return cpu.u;
    case MON_REG_S: return cpu.s;
    case MON_REG_V: return cpu.v;
    case MON_REG_MD: return cpu.md;
    default: return 0;
    }
}

static void reg_set(int id, uint32_t v)
{
    switch (id) {
    case MON_REG_PC: cpu.pc = (uint16_t)v; break;
    case MON_REG_CC: cpu.cc = (uint8_t)v; break;
    case MON_REG_A: cpu.d.ab.a = (uint8_t)v; break;
    case MON_REG_B: cpu.d.ab.b = (uint8_t)v; break;
    case MON_REG_D: cpu.d.d = (uint16_t)v; break;
    case MON_REG_E: cpu_write_e((uint8_t)v); break;
    case MON_REG_F: cpu_write_f((uint8_t)v); break;
    case MON_REG_W:
        cpu_write_e((uint8_t)(v >> 8));
        cpu_write_f((uint8_t)v);
        break;
    case MON_REG_Q: cpu.q = v; break;
    case MON_REG_DP: cpu_set_dp((uint8_t)v); break;
    case MON_REG_X: cpu.x = (uint16_t)v; break;
    case MON_REG_Y: cpu.y = (uint16_t)v; break;
    case MON_REG_U: cpu.u = (uint16_t)v; break;
    case MON_REG_S: cpu.s = (uint16_t)v; break;
    case MON_REG_V: cpu_write_v((uint16_t)v); break;
    case MON_REG_MD: cpu_write_md((uint8_t)v); break;
    default: break;
    }
}

/* A dash field is zero and, for E/F/V/MD, not an explicit write. */
static void reg_apply(int id, uint32_t v, int as_dash)
{
    int which = latent_which(id);
    if (as_dash && which >= 0) {
        if (id == MON_REG_E) cpu.e = 0;
        else if (id == MON_REG_F) cpu.f = 0;
        else if (id == MON_REG_V) cpu.v = 0;
        else cpu.md = 0;
        cpu_hide_6309(which);
        return;
    }
    reg_set(id, v);
}

static int append_ch(char *dst, int *n, size_t cap, char ch)
{
    if (!dst || cap == 0) return 0;
    if ((size_t)(*n + 1) >= cap) return 0;
    dst[(*n)++] = ch;
    dst[*n] = 0;
    return 1;
}

static int field_width_ok(void)
{
    int i;
    int n = 1;
    int nf = reg_nfields();
    for (i = 0; i < nf; i++) {
        n += reg_fields[i].width;
        if (i + 1 < nf) n++;
    }
    return n;
}

void monitor_format_regs(char *names, size_t names_n, char *vals, size_t vals_n)
{
    int i;
    int nn = 0;
    int nv = 0;
    int nf = reg_nfields();
    char flags[8];
    if (names && names_n) names[0] = 0;
    if (vals && vals_n) vals[0] = 0;
    format_flags(flags);
    append_ch(names, &nn, names_n, ' ');
    append_ch(vals, &nv, vals_n, ';');
    for (i = 0; i < nf; i++) {
        int k;
        int width = reg_fields[i].width;
        if (reg_fields[i].flags) {
            for (k = 0; k < width; k++) append_ch(vals, &nv, vals_n, flags[k]);
        } else if (field_latent(reg_fields[i].id)) {
            for (k = 0; k < width; k++) append_ch(vals, &nv, vals_n, '-');
        } else {
            char hex[16];
            uint32_t v = reg_get(reg_fields[i].id);
            snprintf(hex, sizeof hex, "%0*X", width, (unsigned)v);
            for (k = 0; k < width; k++) append_ch(vals, &nv, vals_n, hex[k]);
        }
        for (k = 0; k < width; k++) {
            char ch = (k < (int)strlen(reg_fields[i].name)) ? reg_fields[i].name[k] : ' ';
            append_ch(names, &nn, names_n, ch);
        }
        if (i + 1 < nf) {
            append_ch(names, &nn, names_n, ' ');
            append_ch(vals, &nv, vals_n, ' ');
        }
    }
}

int monitor_reg_value_len(void)
{
    return field_width_ok();
}

int monitor_line_is_regs(const char *s, int n)
{
    int i;
    int col = 0;
    int nf = reg_nfields();
    if (!s || n != field_width_ok() || s[0] != ';') return 0;
    col = 1;
    for (i = 0; i < nf; i++) {
        int k;
        int width = reg_fields[i].width;
        if (col + width > n) return 0;
        for (k = 0; k < width; k++) {
            unsigned char ch = (unsigned char)s[col + k];
            if (reg_fields[i].flags) {
                if (ch != '.' && !strchr("EFHINZVC", ch)) return 0;
            } else if (hexval((char)ch) < 0 &&
                       !(ch == '-' && latent_which(reg_fields[i].id) >= 0)) return 0;
        }
        col += width;
        if (i + 1 < nf) {
            if (col >= n || s[col] != ' ') return 0;
            col++;
        }
    }
    return col == n;
}

int monitor_reg_cells(struct mon_reg_cell *out, int max)
{
    int i;
    int col = 1;
    int n = 0;
    int nf = reg_nfields();
    for (i = 0; i < nf; i++) {
        int k;
        if (!reg_fields[i].flags) {
            for (k = 0; k < reg_fields[i].width; k++) {
                if (n < max && out) {
                    out[n].col = col + k;
                    out[n].id = reg_fields[i].id;
                    out[n].nib = k;
                }
                n++;
            }
        }
        col += reg_fields[i].width;
        if (i + 1 < nf) col++;
    }
    return n;
}

void monitor_poke_reg(int id, int nib, int digit)
{
    int i;
    int width = 0;
    uint32_t cur;
    int shift;
    for (i = 0; i < reg_nfields(); i++) {
        if (reg_fields[i].id == id) {
            width = reg_fields[i].width;
            break;
        }
    }
    if (width < 1 || nib < 0 || nib >= width) return;
    if (digit < 0 || digit > 15) return;
    cur = reg_get(id);
    shift = (width - 1 - nib) * 4;
    cur &= ~(0xFu << shift);
    cur |= ((uint32_t)digit & 0xFu) << shift;
    reg_set(id, cur);
}

static void print_reg_line(void)
{
    char names[96];
    char vals[96];
    monitor_format_regs(names, sizeof names, vals, sizeof vals);
    printf("%s\n%s\n", names, vals);
}

static void cmd_convert(unsigned long v)
{
    unsigned long x = v;
    char bits[40];
    int n = 0;
    if (x == 0) bits[n++] = '0';
    else {
        char rev[40];
        int r = 0;
        while (x && r < (int)sizeof(rev)) {
            rev[r++] = (char)('0' + (x & 1));
            x >>= 1;
        }
        while (r) bits[n++] = rev[--r];
    }
    bits[n] = 0;
    printf("      $%04lX\n", v & 0xFFFFUL);
    printf("      +%lu\n", v);
    printf("      &%lo\n", v);
    printf("      %%%s\n", bits);
}

static int whole_number(const char *line, unsigned long *v)
{
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return 0;
    const char *end = NULL;
    if (!parse_num(p, v, &end)) return 0;
    while (*end && isspace((unsigned char)*end)) end++;
    return *end == 0;
}

static int flag_token(const char *s)
{
    int n = (int)strlen(s);
    int i;
    if (n != 8) return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c != '.' && !strchr("EFHINZVC", c)) return 0;
    }
    return 1;
}

static int dash_token(const char *s)
{
    int i;
    if (!s[0]) return 0;
    for (i = 0; s[i]; i++) if (s[i] != '-') return 0;
    return 1;
}

static int next_reg_token(char **p, char *tok, int cap)
{
    int i = 0;
    skip_ws(p);
    if (!**p || **p == '\n' || **p == '\r') return 0;
    while (**p && !isspace((unsigned char)**p) && i + 1 < cap) tok[i++] = *(*p)++;
    tok[i] = 0;
    return i > 0;
}

static void cmd_edit_regs(char *line)
{
    char *p = line;
    while (*p && *p != ';') p++;
    if (*p == ';') p++;
    if (strchr(p, '=')) {
        char *tok;
        char *save = p;
        while ((tok = strtok(save, " \t\n=,"))) {
            save = NULL;
            char *val = strtok(NULL, " \t\n=,");
            if (!val) break;
            for (char *c = tok; *c; c++) *c = (char)toupper((unsigned char)*c);
            unsigned long nv = 0;
            const char *end = NULL;
            int id = MON_REG_NONE;
            if (!parse_num(val, &nv, &end) || (end && *end)) continue;
            if (strcmp(tok, "PC") == 0) id = MON_REG_PC;
            else if (strcmp(tok, "A") == 0) id = MON_REG_A;
            else if (strcmp(tok, "B") == 0) id = MON_REG_B;
            else if (strcmp(tok, "D") == 0) id = MON_REG_D;
            else if (strcmp(tok, "E") == 0) id = MON_REG_E;
            else if (strcmp(tok, "F") == 0) id = MON_REG_F;
            else if (strcmp(tok, "W") == 0) id = MON_REG_W;
            else if (strcmp(tok, "Q") == 0) id = MON_REG_Q;
            else if (strcmp(tok, "DP") == 0) id = MON_REG_DP;
            else if (strcmp(tok, "X") == 0) id = MON_REG_X;
            else if (strcmp(tok, "Y") == 0) id = MON_REG_Y;
            else if (strcmp(tok, "U") == 0) id = MON_REG_U;
            else if (strcmp(tok, "S") == 0) id = MON_REG_S;
            else if (strcmp(tok, "V") == 0) id = MON_REG_V;
            else if (strcmp(tok, "MD") == 0) id = MON_REG_MD;
            else if (strcmp(tok, "CC") == 0) id = MON_REG_CC;
            if (id != MON_REG_NONE) reg_set(id, (uint32_t)nv);
        }
        print_reg_line();
        return;
    }
    /* Eight numbers keep the old order: PC CC A B X Y U S.
     * The printed value line has the flag letters, then
     * PC CC A B E F DP X Y U S V MD. A dash field is zero. */
    {
        unsigned long vals[16];
        int latent[16];
        int n = 0;
        int saw_flags = 0;
        char tok[32];
        while (n < 16 && next_reg_token(&p, tok, (int)sizeof tok)) {
            unsigned long nv = 0;
            const char *end = NULL;
            if (parse_num(tok, &nv, &end) && end && *end == 0) {
                vals[n] = nv;
                latent[n] = 0;
                n++;
            } else if (dash_token(tok)) {
                vals[n] = 0;
                latent[n] = 1;
                n++;
            } else if (flag_token(tok)) saw_flags = 1;
            else break;
        }
        if (saw_flags || n > 8) {
            static const int order[] = {
                MON_REG_PC, MON_REG_CC, MON_REG_A, MON_REG_B, MON_REG_E,
                MON_REG_F, MON_REG_DP, MON_REG_X, MON_REG_Y, MON_REG_U,
                MON_REG_S, MON_REG_V, MON_REG_MD
            };
            int i;
            for (i = 0; i < n && i < (int)(sizeof order / sizeof order[0]); i++)
                reg_apply(order[i], (uint32_t)vals[i], latent[i]);
        } else {
            if (n > 0) reg_set(MON_REG_PC, (uint32_t)vals[0]);
            if (n > 1) reg_set(MON_REG_CC, (uint32_t)vals[1]);
            if (n > 2) reg_set(MON_REG_A, (uint32_t)vals[2]);
            if (n > 3) reg_set(MON_REG_B, (uint32_t)vals[3]);
            if (n > 4) reg_set(MON_REG_X, (uint32_t)vals[4]);
            if (n > 5) reg_set(MON_REG_Y, (uint32_t)vals[5]);
            if (n > 6) reg_set(MON_REG_U, (uint32_t)vals[6]);
            if (n > 7) reg_set(MON_REG_S, (uint32_t)vals[7]);
        }
    }
    print_reg_line();
}

static void dump_mem_line(uint16_t addr, int nbytes)
{
    printf(">%04X", addr);
    for (int j = 0; j < nbytes; j++) printf(" %02X", mem_read8((uint16_t)(addr + j)));
    printf(": ");
    for (int j = 0; j < nbytes; j++) {
        uint8_t b = mem_read8((uint16_t)(addr + j));
        putchar((b >= 32 && b < 127) ? b : '.');
    }
    putchar('\n');
}

static void cmd_memory(char *line)
{
    char *p = line + 1;
    uint16_t start = next_mem;
    int have_start = next_u16(&p, &start);
    uint16_t end = 0;
    int have_end = next_u16(&p, &end);
    int nbytes = screen_mem_bytes();
    int lines = 12;
    if (nbytes < 1) {
        printf("?\n");
        return;
    }
    if (have_start && have_end) {
        if (end < start) {
            printf("?\n");
            return;
        }
        uint32_t n = (uint32_t)end - start + 1;
        lines = (int)((n + (uint32_t)nbytes - 1) / (uint32_t)nbytes);
        if (lines < 1) lines = 1;
        if (lines > 4096) lines = 4096;
    }
    for (int i = 0; i < lines; i++)
        dump_mem_line((uint16_t)(start + i * nbytes), nbytes);
    next_mem = (uint16_t)(start + lines * nbytes);
}

static void cmd_overtype(char *line)
{
    char *p = line;
    while (*p && *p != '>') p++;
    if (*p == '>') p++;
    uint16_t addr = next_mem;
    if (!next_u16(&p, &addr)) {
        printf("?\n");
        return;
    }
    uint16_t b;
    int any = 0;
    while (next_u16(&p, &b)) {
        mem_write8(addr++, (uint8_t)b);
        any = 1;
        skip_ws(&p);
        if (*p == ':') break;
    }
    if (!any) printf("?\n");
    next_mem = addr;
}

static void cmd_poke(char *line)
{
    uint16_t addr = next_mem;
    char *p = line;
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    uint16_t a;
    if (parse_u16(p, &a, (const char **)&p)) addr = a;
    uint16_t b;
    while (next_u16(&p, &b)) mem_write8(addr++, (uint8_t)b);
    next_mem = addr;
}

static void cmd_disasm(char *line)
{
    char *p = line + 1;
    uint16_t addr = next_dasm ? next_dasm : cpu.pc;
    int have = next_u16(&p, &addr);
    uint16_t end = 0;
    int have_end = next_u16(&p, &end);
    int budget = 20;
    int guard = 0;
    while (guard++ < 200) {
        if (have && have_end && addr > end) break;
        if (!(have && have_end) && budget <= 0) break;
        char text[80];
        char ops[96];
        uint16_t next;
        int n = disasm(addr, text, sizeof(text), &next);
        if (n < 1) n = 1;
        disasm_opfield(ops, sizeof ops, addr, n, text);
        printf(". %04X%s\n", addr, ops);
        budget -= n;
        addr = next;
    }
    next_dasm = addr;
}

static int assemble_show(uint16_t addr, const char *text)
{
    char err[80];
    int n = asm_instr(addr, text, err, sizeof(err));
    if (n < 0) {
        printf("?\n");
        return -1;
    }
    char d[80];
    char ops[96];
    uint16_t nxt = addr;
    disasm(addr, d, sizeof(d), &nxt);
    disasm_opfield(ops, sizeof ops, addr, n, d);
    printf("a %04X%s\n", addr, ops);
    next_asm = (uint16_t)(addr + n);
    next_dasm = next_asm;
    return n;
}

static void cmd_assemble(char *line)
{
    char *p = line + 1;
    uint16_t addr = next_asm ? next_asm : cpu.pc;
    const char *e = NULL;
    skip_ws(&p);
    if (parse_u16(p, &addr, &e)) p = (char *)e;
    skip_ws(&p);
    if (*p && *p != '\n' && *p != '\r') {
        assemble_show(addr, p);
        return;
    }
    /* a <addr> with no mnemonic stays in assemble mode at that address. */
    uint16_t here = addr;
    for (;;) {
        char more[160];
        printf("a %04X ", here);
        fflush(stdout);
        if (screen_is_active()) {
            if (!screen_read_field(more, sizeof(more))) return;
        } else if (!fgets(more, sizeof(more), stdin)) return;
        char *q = more;
        skip_ws(&q);
        if (*q == 0 || *q == '\n' || *q == '\r') return;
        int n = assemble_show(here, q);
        if (n > 0) here = (uint16_t)(here + n);
    }
}

enum {
    RUN_RETURN = 0,
    RUN_SIGINT,
    RUN_BP,
    RUN_HALT,
    RUN_JUMP_FAIL,
    RUN_STOP,
    RUN_NONE
};

static int run_core(uint16_t addr, int is_jump, uint16_t ret)
{
    cpu.pc = addr;
    cpu.halted = false;
    cpu.debug_mode = false;
    cpu.stop_requested = false;
    signal(SIGINT, sigint_handler);
    break_flag = 0;
    uint32_t steps = 0;
    while (!cpu.halted && !break_flag && !cpu.stop_requested) {
        if (is_jump && cpu.pc == ret) break;
        if (bp_check_exec(cpu.pc)) {
            cpu.stop_requested = true;
            break;
        }
        cpu_step();
        acia_poll_input();
        if (is_jump && ++steps > 2000000u) break;
    }
    signal(SIGINT, SIG_DFL);
    acia_end_run();
    if (is_jump && cpu.pc == ret) return RUN_RETURN;
    if (break_flag) return RUN_SIGINT;
    if (bp_check_exec(cpu.pc)) return RUN_BP;
    if (cpu.halted) return RUN_HALT;
    if (is_jump) return RUN_JUMP_FAIL;
    if (cpu.stop_requested) return RUN_STOP;
    return RUN_NONE;
}

static void report_run(int why, int is_jump, uint16_t addr)
{
    if (is_jump && why == RUN_RETURN) printf("Returned from %04X\n", addr);
    else if (why == RUN_SIGINT) printf("\n** Break (SIGINT) at %04X **\n", cpu.pc);
    else if (why == RUN_BP) printf("\n** Breakpoint at %04X **\n", cpu.pc);
    else if (why == RUN_HALT) printf("\n** Halted at %04X **\n", cpu.pc);
    else if (why == RUN_JUMP_FAIL) printf("?\n");
    else if (why == RUN_STOP) printf("\n** Stopped at %04X **\n", cpu.pc);
    print_reg_line();
    next_dasm = cpu.pc;
    cpu.debug_mode = true;
}

/* The editor and the ACIA both own the terminal. Leave the editor before
 * a run so acia set_raw saves the cooked state, then draw the buffer again. */
static void run_and_report(uint16_t addr, int is_jump, uint16_t ret)
{
    int was = screen_is_active();
    int why;
    if (was) screen_suspend();
    why = run_core(addr, is_jump, ret);
    if (was) screen_resume();
    report_run(why, is_jump, addr);
}

static void cmd_go(char *line)
{
    char *p = line + 1;
    uint16_t addr = cpu.pc;
    next_u16(&p, &addr);
    printf("Running from %04X. Ctrl-C to break.\n", addr);
    run_and_report(addr, 0, 0);
}

static void cmd_jump(char *line)
{
    char *p = line + 1;
    uint16_t addr = cpu.pc;
    next_u16(&p, &addr);
    if (cpu.s < 2) {
        printf("?\n");
        return;
    }
    /* Same byte order as RTS: the high byte is at S, the low byte at S+1. */
    uint16_t ret = 0x0000;
    cpu.s--;
    mem_write8(cpu.s, (uint8_t)ret);
    cpu.s--;
    mem_write8(cpu.s, (uint8_t)(ret >> 8));
    run_and_report(addr, 1, ret);
}

static void cmd_step(char *line)
{
    char *p = line + 1;
    int n = 1;
    skip_ws(&p);
    if (*p && isdigit((unsigned char)*p)) n = atoi(p);
    if (n < 1) n = 1;
    cpu.debug_mode = true;
    for (int i = 0; i < n; i++) {
        if (cpu.halted) break;
        int c = cpu_step();
        char d[80];
        uint16_t nxt;
        disasm(cpu.pc, d, sizeof(d), &nxt);
        printf("%04X  %s  (cycles %d)\n", cpu.pc, d, c);
    }
    print_reg_line();
    next_dasm = cpu.pc;
}

static void cmd_break(char *line)
{
    char *p = line + 1;
    uint16_t a = 0;
    if (!next_u16(&p, &a)) {
        printf("Breakpoints:\n");
        for (int i = 0; i < 32; i++) if (breakpoints[i].used)
            printf("  %04X\n", breakpoints[i].addr);
        return;
    }
    for (int i = 0; i < 32; i++) {
        if (breakpoints[i].used && breakpoints[i].addr == a) {
            breakpoints[i].used = 0;
            bp_clear_exec(a);
            printf("Cleared breakpoint at %04X\n", a);
            return;
        }
    }
    for (int i = 0; i < 32; i++) if (!breakpoints[i].used) {
        breakpoints[i].addr = a;
        breakpoints[i].used = 1;
        bp_set_exec(a);
        printf("Breakpoint set at %04X\n", a);
        return;
    }
    printf("Breakpoint table full\n");
}

static void cmd_break_clear(void)
{
    for (int i = 0; i < 32; i++) breakpoints[i].used = 0;
    bp_clear_all();
    printf("All breakpoints cleared.\n");
}

static void cmd_fill(char *line)
{
    char *p = line + 1;
    uint16_t s, e, v;
    if (!next_u16(&p, &s) || !next_u16(&p, &e) || !next_u16(&p, &v) || e < s) {
        printf("f <start> <end> <val>\n");
        return;
    }
    for (uint32_t a = s; a <= e; a++) mem_write8((uint16_t)a, (uint8_t)v);
    printf("Filled %04X..%04X with %02X\n", s, e, (uint8_t)v);
}

static void cmd_hunt(char *line)
{
    char *p = line + 1;
    uint16_t s, e;
    if (!next_u16(&p, &s) || !next_u16(&p, &e) || e < s) {
        printf("h <start> <end> '<text>    or    h <start> <end> <bytes>\n");
        return;
    }
    skip_ws(&p);
    uint8_t pat[32];
    int np = 0;
    if (*p == '\'' || *p == '"') {
        p++;
        while (*p && *p != '\n' && *p != '\r' && np < 32) pat[np++] = (uint8_t)*p++;
    } else {
        uint16_t b;
        while (np < 32 && next_u16(&p, &b)) pat[np++] = (uint8_t)b;
    }
    if (np < 1) {
        printf("h <start> <end> '<text>    or    h <start> <end> <bytes>\n");
        return;
    }
    for (uint32_t a = s; a + (uint32_t)np - 1 <= e; a++) {
        int ok = 1;
        for (int i = 0; i < np; i++) {
            if (mem_read8((uint16_t)(a + (uint32_t)i)) != pat[i]) { ok = 0; break; }
        }
        if (ok) printf("%04X\n", (unsigned)a);
    }
}

static void cmd_transfer(char *line)
{
    char *p = line + 1;
    uint16_t s, e, d;
    if (!next_u16(&p, &s) || !next_u16(&p, &e) || !next_u16(&p, &d) || e < s) {
        printf("t <start> <end> <dest>\n");
        return;
    }
    uint32_t n = (uint32_t)e - s + 1;
    uint8_t *buf = malloc(n);
    if (!buf) { printf("?\n"); return; }
    for (uint32_t i = 0; i < n; i++) buf[i] = mem_read8((uint16_t)(s + i));
    for (uint32_t i = 0; i < n; i++) mem_write8((uint16_t)(d + i), buf[i]);
    free(buf);
    printf("Transferred %04X..%04X to %04X\n", s, e, d);
}

static void cmd_compare(char *line)
{
    char *p = line + 1;
    uint16_t s, e, d;
    if (!next_u16(&p, &s) || !next_u16(&p, &e) || !next_u16(&p, &d) || e < s) {
        printf("c <start> <end> <dest>\n");
        return;
    }
    for (uint32_t a = s; a <= e; a++) {
        if (mem_read8((uint16_t)a) != mem_read8((uint16_t)(d + (a - s))))
            printf("%04X\n", (unsigned)a);
    }
}

static void cmd_load(char *line)
{
    char *p = line + 1;
    skip_ws(&p);
    if (!*p || *p == '\n') {
        printf("l <file> [addr]\n");
        return;
    }
    char fname[256];
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < sizeof(fname)) fname[i++] = *p++;
        fname[i] = 0;
        if (*p == '"') p++;
    } else {
        size_t i = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && i + 1 < sizeof(fname))
            fname[i++] = *p++;
        fname[i] = 0;
    }
    uint16_t forced = 0;
    int have = next_u16(&p, &forced);
    int n = load_file(fname, have ? forced : 0, have);
    if (n < 0) printf("Load failed: %s\n", fname);
    else printf("Loaded %d bytes from %s\n", n, fname);
}

static int is_step_arg(char *p)
{
    skip_ws(&p);
    if (!*p || *p == '\n' || *p == '\r') return 1;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    while (*p && isspace((unsigned char)*p)) p++;
    return *p == 0 || *p == '\n' || *p == '\r';
}

static void cmd_save(char *line)
{
    char *p = line + 1;
    skip_ws(&p);
    char fname[256];
    int exclusive = 0;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < sizeof(fname)) fname[i++] = *p++;
        fname[i] = 0;
        if (*p == '"') p++;
        if (*p == ',') {
            p++;
            uint16_t dev;
            next_u16(&p, &dev);
            if (*p == ',') p++;
            exclusive = 1;
        }
    } else {
        size_t i = 0;
        while (*p && !isspace((unsigned char)*p) && i + 1 < sizeof(fname)) fname[i++] = *p++;
        fname[i] = 0;
    }
    uint16_t s, e;
    if (!next_u16(&p, &s)) {
        printf("s <file> <start> <end> [S19|IHEX|BIN]\n");
        return;
    }
    if (*p == ',') p++;
    if (!next_u16(&p, &e)) {
        printf("s <file> <start> <end> [S19|IHEX|BIN]\n");
        return;
    }
    if (exclusive) {
        if (e == s) { printf("?\n"); return; }
        e = (uint16_t)(e - 1);
    }
    char fmt[16] = "S19";
    skip_ws(&p);
    if (*p && *p != '\n' && *p != '\r') {
        size_t i = 0;
        while (*p && !isspace((unsigned char)*p) && i + 1 < sizeof(fmt)) fmt[i++] = *p++;
        fmt[i] = 0;
    }
    int rc = save_file(fname, s, e, fmt);
    if (rc < 0) printf("Save failed\n");
    else printf("Saved %04X..%04X to %s (%s)\n", s, e, fname, fmt);
}

static void cmd_shell(char *line)
{
    char *cmd = line + 1;
    int was;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    if (!*cmd) {
        printf("! <command>\n");
        return;
    }
    was = screen_is_active();
    if (was) screen_suspend();
    system(cmd);
    if (was) screen_after_external();
}

static void help(void)
{
    puts("Mimic SuperMon");
    puts("  $ + & %           number conversion (default hex)");
    puts("  r                 registers. ; overtypes that line");
    puts("  m [start [end]]   8-byte lines. one address shows 12 lines");
    puts("  >addr bytes       overtype a memory line");
    puts("  a <addr> <mnem>   assemble one 6809 or 6309 instruction");
    puts("  d [start [end]]   disassemble. one address shows 20 bytes");
    puts("  f <s> <e> <val>   fill");
    puts("  h <s> <e> <pat>   hunt. 'text or bytes");
    puts("  t <s> <e> <dest>  transfer");
    puts("  c <s> <e> <dest>  compare");
    puts("  g [addr]          go");
    puts("  j <addr>          jump to a subroutine, then return");
    puts("  l <file> [addr]   load a POSIX file");
    puts("  s <file> <s> <e> [fmt]  save a POSIX file");
    puts("  x                 exit");
    puts("  @                 host files are l and s");
    puts("  ?                 this help");
    puts("  arrows            edit ; registers and > hex; Return on . assembles");
    puts("Mimic: S [n] step, B [addr] breakpoint, BC clear, ! shell, : poke, Q exit");
}

static int word_is(const char *line, const char *w)
{
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t n = strlen(w);
    if (strncasecmp(p, w, n) != 0) return 0;
    p += n;
    return *p == 0 || isspace((unsigned char)*p) || *p == '\r' || *p == '\n';
}

/* A, B, C, D, and F are hex digits. A bare command must win over conversion. */
static int is_command_line(const char *line)
{
    if (word_is(line, "BC") || word_is(line, "IRQ") || word_is(line, "FIRQ") || word_is(line, "NMI"))
        return 1;
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isalpha((unsigned char)*p)) return 0;
    char c = (char)toupper((unsigned char)*p);
    p++;
    if (*p && !isspace((unsigned char)*p) && *p != '\r' && *p != '\n') return 0;
    return strchr("ABCDFGHLMRSTXJQ", c) != NULL;
}

void monitor_run(void)
{
    char line[256];
    int tty = screen_open();
    cpu.debug_mode = true;
    next_dasm = cpu.pc;
    next_mem = 0;
    next_asm = cpu.pc;

    printf("Mimic SuperMon. Type ? for help. X to exit.\n");
    print_reg_line();

    for (;;) {
        if (tty && screen_is_active()) {
            if (!screen_readline(line, sizeof line)) break;
        } else {
            if (!fgets(line, sizeof(line), stdin)) break;
        }
        unsigned long conv = 0;
        if (!is_command_line(line) && whole_number(line, &conv)) {
            cmd_convert(conv);
            continue;
        }
        if (word_is(line, "IRQ")) { cpu_assert_irq(); continue; }
        if (word_is(line, "FIRQ")) { cpu_assert_firq(); continue; }
        if (word_is(line, "NMI")) { cpu_assert_nmi(); continue; }
        if (word_is(line, "BC")) { cmd_break_clear(); continue; }

        char c = command_char(line);
        if (c == '\n' || c == 0) continue;
        if (c == 'Q' || c == 'X') break;
        else if (c == 'R') print_reg_line();
        else if (c == ';') cmd_edit_regs(line);
        else if (c == 'M') cmd_memory(line);
        else if (c == '>') cmd_overtype(line);
        else if (c == ':') cmd_poke(line);
        else if (c == 'D') cmd_disasm(line);
        else if (c == 'A') cmd_assemble(line);
        else if (c == 'G') cmd_go(line);
        else if (c == 'J') cmd_jump(line);
        else if (c == 'F') cmd_fill(line);
        else if (c == 'H') cmd_hunt(line);
        else if (c == 'T') cmd_transfer(line);
        else if (c == 'C') cmd_compare(line);
        else if (c == 'L') cmd_load(line);
        else if (c == 'S') {
            char *p = line + 1;
            if (is_step_arg(p)) cmd_step(line);
            else cmd_save(line);
        }
        else if (c == 'B') cmd_break(line);
        else if (c == '!') cmd_shell(line);
        else if (c == '@') puts("@ is not a host disk. Use l and s for files.");
        else if (c == '?') help();
        else printf("?\n");
    }
    screen_close();
    puts("Exiting mimic09.");
}
