/* xc9_3.c — Wintek 6809 code generator (icf1/icf2 → a9 assembly).
 *
 * Usage: xc9_3 icfile1 icfile2 asmfile [-v] [-ns] [-ni] [-nh] [-np] [-al]
 *
 * Calls use the RNLIB9 convention: push each 16-bit argument left to
 * right, A = byte count, JSR callee. The callee pops the arguments.
 * Every function checks A and calls .LINK when the count is wrong.
 * main(void) is entered from cstart with A = 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "util.h"

#define MAXF    32
#define MAXSTMT 256
#define MAXARG  256
#define MAXSTR  64
#define MAXSB   8192
#define MAXNAME 64

enum { ST_CALL = 1, ST_RET = 2, AR_STR = 1, AR_INT = 2 };

static const char *VERSION = "1.0 r3";
static const char *COPYRIGHT = "Copyright 2026 Binary Systems, Inc.";

typedef struct {
    char name[MAXNAME];
    int params;
    int s0, ns;
} func_t;

typedef struct {
    int kind;
    char name[MAXNAME];
    int narg, arg0;
    int has, val;
} stmt_t;

typedef struct {
    int kind, ival, sid;
} arg_t;

typedef struct {
    int len, off;
} str_t;

static func_t funcs[MAXF];
static int nfunc;
static stmt_t stmts[MAXSTMT];
static int nstmt;
static arg_t args[MAXARG];
static int narg;
static str_t strs[MAXSTR];
static int nstr;
static unsigned char pool[MAXSB];
static int npool;
static unsigned prog_bytes;

static void add_bytes(unsigned n) { prog_bytes += n; }

static int defined_here(const char *name) {
    for (int i = 0; i < nfunc; i++)
        if (strcmp(funcs[i].name, name) == 0) return 1;
    return 0;
}

static int load_tree(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die("CODE GEN: FATAL ERROR - can't open %s", path);
    char line[512];
    func_t *cur = NULL;
    while (fgets(line, sizeof(line), f)) {
        char w1[64];
        int a = 0, b = 0;
        if (sscanf(line, "FUNC %63s %d", w1, &a) == 2) {
            if (nfunc >= MAXF) die("out of memory");
            cur = &funcs[nfunc++];
            memset(cur, 0, sizeof(*cur));
            snprintf(cur->name, sizeof(cur->name), "%s", w1);
            cur->params = a;
            cur->s0 = nstmt;
            continue;
        }
        if (sscanf(line, "CALL %63s %d", w1, &a) == 2) {
            if (!cur || nstmt >= MAXSTMT) die("out of memory");
            stmt_t *s = &stmts[nstmt++];
            memset(s, 0, sizeof(*s));
            s->kind = ST_CALL;
            snprintf(s->name, sizeof(s->name), "%s", w1);
            s->narg = a;
            s->arg0 = narg;
            cur->ns++;
            continue;
        }
        if (sscanf(line, "ARG %63s %d", w1, &a) == 2) {
            if (narg >= MAXARG) die("out of memory");
            arg_t *g = &args[narg++];
            memset(g, 0, sizeof(*g));
            if (strcmp(w1, "STR") == 0) { g->kind = AR_STR; g->sid = a; }
            else { g->kind = AR_INT; g->ival = a; }
            continue;
        }
        if (sscanf(line, "RET %d %d", &a, &b) == 2) {
            if (!cur || nstmt >= MAXSTMT) die("out of memory");
            stmt_t *s = &stmts[nstmt++];
            memset(s, 0, sizeof(*s));
            s->kind = ST_RET;
            s->has = a;
            s->val = b;
            cur->ns++;
            continue;
        }
        if (strncmp(line, "ENDFUNC", 7) == 0) cur = NULL;
    }
    fclose(f);
    return 1;
}

static int load_strs(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die("CODE GEN: FATAL ERROR - can't open %s", path);
    char line[MAXSB * 2];
    while (fgets(line, sizeof(line), f)) {
        int id = 0, len = 0;
        char *p = line;
        if (sscanf(p, "%d %d", &id, &len) != 2) continue;
        if (id < 0 || id >= MAXSTR || len < 0 || npool + len > MAXSB) die("out of memory: can't build string");
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        while (*p && *p != ' ') p++;
        strs[id].off = npool;
        strs[id].len = len;
        for (int i = 0; i < len; i++) {
            while (*p == ' ') p++;
            unsigned v = 0;
            if (sscanf(p, "%x", &v) != 1) die("error or early EOF on intermediate code file");
            pool[npool++] = (unsigned char)v;
            while (*p && *p != ' ' && *p != '\n') p++;
        }
        if (id >= nstr) nstr = id + 1;
    }
    fclose(f);
    return 1;
}

static void emit_strs(FILE *o) {
    for (int i = 0; i < nstr; i++) {
        fprintf(o, "S%d\n", i);
        int col = 0;
        for (int b = 0; b < strs[i].len; b++) {
            if (col == 0) fprintf(o, "\tFCB\t");
            else fputc(',', o);
            fprintf(o, "$%02X", pool[strs[i].off + b]);
            add_bytes(1);
            if (++col == 12) { fputc('\n', o); col = 0; }
        }
        if (col) fputc('\n', o);
        fprintf(o, "\tFCB\t0\n");
        add_bytes(1);
    }
}

static void emit_call(FILE *o, const stmt_t *s) {
    for (int i = 0; i < s->narg; i++) {
        arg_t *g = &args[s->arg0 + i];
        if (g->kind == AR_STR) {
            fprintf(o, "\tLDX\t#S%d\n", g->sid);
            fprintf(o, "\tPSHS\tX\n");
            add_bytes(5);
        } else {
            fprintf(o, "\tLDD\t#%d\n", (int)(int16_t)(g->ival & 0xFFFF));
            fprintf(o, "\tPSHS\tD\n");
            add_bytes(5);
        }
    }
    fprintf(o, "\tLDA\t#%d\n", s->narg * 2);
    fprintf(o, "\tJSR\t%s\n", s->name);
    add_bytes(5);
}

static void emit_ret(FILE *o, const func_t *f, const stmt_t *s) {
    if (!s->has || s->val == 0) {
        fprintf(o, "\tCLRA\n\tCLRB\n");
        add_bytes(2);
    } else {
        fprintf(o, "\tLDD\t#%d\n", (int)(int16_t)(s->val & 0xFFFF));
        add_bytes(3);
    }
    if (f->params == 0) {
        fprintf(o, "\tRTS\n");
        add_bytes(1);
    } else {
        fprintf(o, "\tPULS\tX\n");
        fprintf(o, "\tLEAS\t%d,S\n", f->params);
        fprintf(o, "\tJMP\t0,X\n");
        add_bytes(6);
    }
}

static void emit_asm(FILE *o, const char *nam) {
    fprintf(o, "\tNAM\t%s\n", nam);
    fprintf(o, "\tGLOBAL\t");
    for (int i = 0; i < nfunc; i++)
        fprintf(o, "%s%s", i ? "," : "", funcs[i].name);
    fputc('\n', o);
    int any = 0;
    fprintf(o, "\tEXTERN\t.LINK");
    any = 1;
    for (int i = 0; i < nstmt; i++) {
        if (stmts[i].kind != ST_CALL) continue;
        if (defined_here(stmts[i].name)) continue;
        int seen = 0;
        for (int k = 0; k < i; k++)
            if (stmts[k].kind == ST_CALL && strcmp(stmts[k].name, stmts[i].name) == 0)
                seen = 1;
        if (seen) continue;
        fprintf(o, ",%s", stmts[i].name);
        any = 1;
    }
    if (any) fputc('\n', o);
    fprintf(o, "\tPSCT\n");
    for (int i = 0; i < nfunc; i++) {
        func_t *f = &funcs[i];
        fprintf(o, "%s\tSUBA\t#%d\n", f->name, f->params);
        fprintf(o, "\tBEQ\t%s_go\n", f->name);
        fprintf(o, "\tJSR\t.LINK\n");
        fprintf(o, "%s_go\n", f->name);
        add_bytes(7);
        int saw_ret = 0;
        for (int k = 0; k < f->ns; k++) {
            stmt_t *s = &stmts[f->s0 + k];
            if (s->kind == ST_CALL) emit_call(o, s);
            else if (s->kind == ST_RET) { emit_ret(o, f, s); saw_ret = 1; }
        }
        if (!saw_ret) {
            fprintf(o, "\tCLRA\n\tCLRB\n\tRTS\n");
            add_bytes(3);
        }
    }
    emit_strs(o);
    fprintf(o, "\tEND\n");
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: xc9_3 icfile1 icfile2 asmfile [-v] [-ns] [-ni] [-nh] [-np] [-al]\n");
    fprintf(stderr, "       (%s)\n", argv0);
    exit(1);
}

int main(int argc, char **argv) {
    const char *ic1 = NULL, *ic2 = NULL, *asmf = NULL;
    int noheader = 0, verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-nh") == 0) noheader = 1;
        else if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else if (strcmp(argv[i], "-ns") == 0 || strcmp(argv[i], "-ni") == 0 ||
                 strcmp(argv[i], "-np") == 0 || strcmp(argv[i], "-al") == 0) {
            /* accepted, no separate listing in this subset */
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
        } else if (!ic1) ic1 = argv[i];
        else if (!ic2) ic2 = argv[i];
        else if (!asmf) asmf = argv[i];
        else usage(argv[0]);
    }
    if (!noheader) banner("Binary Systems, Inc. 6809 Code Generator", VERSION, COPYRIGHT);
    if (!ic1 || !ic2 || !asmf) usage(argv[0]);
    load_tree(ic1);
    load_strs(ic2);
    if (nfunc == 0) die("error or early EOF on intermediate code file");
    FILE *o = fopen(asmf, "w");
    if (!o) die("CODE GEN: FATAL ERROR - can't open %s", asmf);
    const char *base = asmf;
    for (const char *p = asmf; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    char nam[64];
    snprintf(nam, sizeof(nam), "%s", base);
    char *dot = strrchr(nam, '.');
    if (dot) *dot = 0;
    if (!nam[0]) snprintf(nam, sizeof(nam), "main");
    emit_asm(o, nam);
    fclose(o);
    if (!noheader || verbose)
        printf(" End code-gen.  %5u program bytes generated.\n", prog_bytes);
    return 0;
}
