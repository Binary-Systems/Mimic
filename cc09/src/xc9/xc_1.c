/* xc_1.c — Wintek C pass 1 (C source → icf1/icf2).
 *
 * Usage: xc_1 icfile1 icfile2 srcfile [-v] [-nh] [-Ipath]
 *
 * Subset that hello.c and the same shape of program need:
 *   shebang line, comments, int/void functions, string and integer
 *   arguments, calls, and return. Strings land in icfile2; the call
 *   tree lands in icfile1 for xc9_3.
 *
 * Undeclared calls are left as externals (K&R implicit int). This is
 * not a full Lattice front end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "util.h"

#define MAXF    32
#define MAXSTMT 256
#define MAXARG  256
#define MAXSTR  64
#define MAXSB   8192
#define MAXNAME 64

enum { ST_CALL = 1, ST_RET = 2 };
enum { AR_STR = 1, AR_INT = 2 };
enum { T_EOF = 0, T_IDENT, T_NUM, T_STR, T_OTHER };

static const char *VERSION = "1.0 r3";
static const char *COPYRIGHT = "Copyright 2026 Binary Systems, Inc.";

static char srcbuf[65536];
static const char *sp;
static int errors;
static int noheader;

static int tk;
static char ttext[MAXSB];
static long tnum;
static unsigned char sbytes[MAXSB];
static int slen;

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

static void err(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    errors++;
}

static void skip_ws_comments(void) {
    for (;;) {
        while (*sp && isspace((unsigned char)*sp)) sp++;
        if (sp[0] == '/' && sp[1] == '/') {
            while (*sp && *sp != '\n') sp++;
            continue;
        }
        if (sp[0] == '/' && sp[1] == '*') {
            sp += 2;
            while (*sp && !(sp[0] == '*' && sp[1] == '/')) sp++;
            if (*sp) sp += 2;
            else { err("comment not terminated"); return; }
            continue;
        }
        return;
    }
}

static int hexdig(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_escape(void) {
    if (*sp != '\\') return (unsigned char)*sp++;
    sp++;
    if (!*sp) return -1;
    char c = *sp++;
    switch (c) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'b': return '\b';
    case 'f': return '\f';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7': {
        int v = c - '0';
        for (int k = 0; k < 2 && *sp >= '0' && *sp <= '7'; k++)
            v = (v << 3) + (*sp++ - '0');
        return v & 0xFF;
    }
    default:
        fprintf(stderr, "WARNING - unknown character escape sequence\n");
        return (unsigned char)c;
    }
}

static void next_tok(void) {
    skip_ws_comments();
    if (!*sp) { tk = T_EOF; ttext[0] = 0; return; }
    if (isalpha((unsigned char)*sp) || *sp == '_') {
        int n = 0;
        while (isalnum((unsigned char)*sp) || *sp == '_') {
            if (n < MAXNAME - 1) ttext[n++] = *sp;
            sp++;
        }
        ttext[n] = 0;
        tk = T_IDENT;
        return;
    }
    if (*sp == '"') {
        sp++;
        slen = 0;
        while (*sp && *sp != '"') {
            if (*sp == '\n') { err("string not terminated"); break; }
            int ch = decode_escape();
            if (ch < 0) { err("string not terminated"); break; }
            if (slen < MAXSB) sbytes[slen++] = (unsigned char)ch;
            else { err("string too long"); return; }
        }
        if (*sp == '"') sp++;
        else err("string not terminated");
        tk = T_STR;
        return;
    }
    if (*sp == '-' || isdigit((unsigned char)*sp)) {
        int sign = 1;
        if (*sp == '-') { sign = -1; sp++; }
        if (!isdigit((unsigned char)*sp)) { tk = T_OTHER; ttext[0] = '-'; ttext[1] = 0; return; }
        long v = 0;
        if (sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X')) {
            sp += 2;
            if (hexdig(*sp) < 0) { err("invalid numeric constant"); }
            while (hexdig(*sp) >= 0) v = (v << 4) + hexdig(*sp++);
        } else {
            while (isdigit((unsigned char)*sp)) v = v * 10 + (*sp++ - '0');
        }
        tnum = sign < 0 ? -v : v;
        tk = T_NUM;
        return;
    }
    ttext[0] = *sp++;
    ttext[1] = 0;
    tk = T_OTHER;
}

static int kw(const char *s) { return tk == T_IDENT && strcmp(ttext, s) == 0; }
static int op(char c) { return tk == T_OTHER && ttext[0] == c; }

static int add_str(void) {
    if (nstr >= MAXSTR || npool + slen > MAXSB) { err("out of memory, can't build string entry"); return 0; }
    strs[nstr].off = npool;
    strs[nstr].len = slen;
    memcpy(pool + npool, sbytes, (size_t)slen);
    npool += slen;
    return nstr++;
}

static int parse_arg(void) {
    if (narg >= MAXARG) { err("out of memory, can't build expression tree"); return -1; }
    arg_t *a = &args[narg];
    memset(a, 0, sizeof(*a));
    if (tk == T_STR) {
        a->kind = AR_STR;
        a->sid = add_str();
        next_tok();
        return narg++;
    }
    if (tk == T_NUM) {
        a->kind = AR_INT;
        a->ival = (int)(tnum & 0xFFFF);
        next_tok();
        return narg++;
    }
    err("expression too complex");
    return -1;
}

static void parse_block(void);

static void parse_stmt(void) {
    if (op('{')) { parse_block(); return; }
    if (nstmt >= MAXSTMT) { err("out of memory, can't build expression tree"); return; }
    stmt_t *s = &stmts[nstmt];
    memset(s, 0, sizeof(*s));
    if (kw("return")) {
        next_tok();
        s->kind = ST_RET;
        if (!op(';')) {
            if (tk != T_NUM) { err("expression too complex"); return; }
            s->has = 1;
            s->val = (int)(tnum & 0xFFFF);
            next_tok();
        }
        if (!op(';')) { err("missing ';' or '{'"); return; }
        next_tok();
        nstmt++;
        return;
    }
    if (tk != T_IDENT) { err("unknown token"); return; }
    snprintf(s->name, sizeof(s->name), "%s", ttext);
    next_tok();
    if (!op('(')) { err("missing ';' or '{'"); return; }
    next_tok();
    s->kind = ST_CALL;
    s->arg0 = narg;
    if (!op(')')) {
        for (;;) {
            int ai = parse_arg();
            if (ai < 0) return;
            s->narg++;
            if (op(')')) break;
            if (!op(',')) { err("missing ';' or '{'"); return; }
            next_tok();
        }
    }
    next_tok();
    if (!op(';')) { err("missing ';' or '{'"); return; }
    next_tok();
    nstmt++;
}

static void parse_block(void) {
    if (!op('{')) { err("missing '{'"); return; }
    next_tok();
    while (tk != T_EOF && !op('}') && errors == 0) parse_stmt();
    if (errors) return;
    if (!op('}')) { err("missing '}'"); return; }
    next_tok();
}

static int type_kw(void) {
    return kw("int") || kw("void") || kw("char");
}

static void parse_function(void) {
    if (nfunc >= MAXF) { err("out of memory, can't create symbol table entry"); return; }
    func_t *f = &funcs[nfunc];
    memset(f, 0, sizeof(*f));
    if (type_kw()) next_tok();
    if (tk != T_IDENT) { err("missing identifier"); return; }
    snprintf(f->name, sizeof(f->name), "%s", ttext);
    next_tok();
    if (!op('(')) { err("missing ';' or '{'"); return; }
    next_tok();
    if (kw("void")) {
        next_tok();
        if (!op(')')) { err("missing ';' or '{'"); return; }
    }
    if (!op(')')) {
        while (!op(')') && tk != T_EOF) {
            if (type_kw()) next_tok();
            if (tk != T_IDENT) { err("missing identifier"); return; }
            next_tok();
            f->params += 2; /* each argument is a 16-bit stack slot */
            if (op(')')) break;
            if (!op(',')) { err("missing ';' or '{'"); return; }
            next_tok();
        }
    }
    if (!op(')')) { err("missing ';' or '{'"); return; }
    next_tok();
    f->s0 = nstmt;
    parse_block();
    f->ns = nstmt - f->s0;
    nfunc++;
}

static void parse_unit(void) {
    next_tok();
    while (tk != T_EOF && errors == 0) parse_function();
}

static int write_icf(const char *tree, const char *stab) {
    FILE *ft = fopen(tree, "w");
    FILE *fs = fopen(stab, "w");
    if (!ft || !fs) {
        if (ft) fclose(ft);
        if (fs) fclose(fs);
        err("can't open intermediate code file");
        return 0;
    }
    for (int i = 0; i < nfunc; i++) {
        func_t *f = &funcs[i];
        fprintf(ft, "FUNC %s %d\n", f->name, f->params);
        for (int k = 0; k < f->ns; k++) {
            stmt_t *s = &stmts[f->s0 + k];
            if (s->kind == ST_CALL) {
                fprintf(ft, "CALL %s %d\n", s->name, s->narg);
                for (int a = 0; a < s->narg; a++) {
                    arg_t *g = &args[s->arg0 + a];
                    if (g->kind == AR_STR) fprintf(ft, "ARG STR %d\n", g->sid);
                    else fprintf(ft, "ARG INT %d\n", g->ival);
                }
            } else if (s->kind == ST_RET) {
                fprintf(ft, "RET %d %d\n", s->has, s->val & 0xFFFF);
            }
        }
        fprintf(ft, "ENDFUNC\n");
    }
    for (int i = 0; i < nstr; i++) {
        fprintf(fs, "%d %d", i, strs[i].len);
        for (int b = 0; b < strs[i].len; b++)
            fprintf(fs, " %02X", pool[strs[i].off + b]);
        fputc('\n', fs);
    }
    fclose(ft);
    fclose(fs);
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: xc_1 icfile1 icfile2 srcfile [-v] [-nh] [-Ipath]\n");
    fprintf(stderr, "       (%s)\n", argv0);
    exit(1);
}

int main(int argc, char **argv) {
    const char *ic1 = NULL, *ic2 = NULL, *src = NULL;
    int verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else if (strcmp(argv[i], "-nh") == 0) noheader = 1;
        else if (strncmp(argv[i], "-I", 2) == 0) {
            if (argv[i][2] == 0 && i + 1 < argc) i++;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown command line option\n");
            usage(argv[0]);
        } else if (!ic1) ic1 = argv[i];
        else if (!ic2) ic2 = argv[i];
        else if (!src) src = argv[i];
        else usage(argv[0]);
    }
    if (!noheader) banner("Binary Systems, Inc. C Compiler, Pass 1", VERSION, COPYRIGHT);
    if (!ic1 || !ic2 || !src) usage(argv[0]);
    FILE *f = fopen(src, "rb");
    if (!f) {
        fprintf(stderr, "can't open input file\n");
        return 1;
    }
    size_t n = fread(srcbuf, 1, sizeof(srcbuf) - 1, f);
    fclose(f);
    srcbuf[n] = 0;
    sp = srcbuf;
    if (sp[0] == '#' && sp[1] == '!') {
        while (*sp && *sp != '\n') sp++;
    }
    parse_unit();
    if (errors || nfunc == 0) {
        fprintf(stderr, " Errors detected.  Compilation aborted.\n");
        return 1;
    }
    if (!write_icf(ic1, ic2)) return 1;
    if (!noheader || verbose)
        printf(" End pass 1.  No errors detected.\n");
    return 0;
}
