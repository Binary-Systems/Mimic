/* xld.c — Wintek 6809 relocatable linker.
 *
 * Reads A9 .rel files (S0/S4/S8/S2/S7/SD/S9), places BSCT at page 0,
 * other relative sections from $0100 in S4 order, and absolute sections
 * at their ORG. A scanned Cx byte is (0xC0|section): C0 is an external
 * hole whose two data bytes are an addend, and C1..CF are a raw 16-bit
 * offset into that section. A non-marker byte after a marker is a shield
 * count of raw literals followed by one literal opcode. S7 adds the
 * external symbol to the addend. Writes absolute S1 records plus a map.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "srec.h"
#include "util.h"

static const char *VERSION = "2.1r0";
static const char *COPYRIGHT = "Copyright 2026 Binary Systems, Inc.";

#define MAXC  512
#define MAXS  4096
#define MAXF  4096
#define MAXK  4096
#define MAXN  32
#define MAXSEC 32
#define MAXMOD 64

typedef struct {
    int sec, size, abs, org, base, start, mod;
} contrib_t;

typedef struct {
    char name[MAXN];
    int sec, off, ci, mod;
} sym_t;

typedef struct {
    char name[MAXN];
    int sec, off, ci, mod;
} fix_t;

typedef struct {
    int sec, off, ci, mod, len;
    uint8_t data[128];
} chunk_t;

static contrib_t contribs[MAXC];
static int ncon;
static sym_t symbols[MAXS];
static int nsym;
static fix_t fixups[MAXF];
static int nfix;
static chunk_t chunks[MAXK];
static int nchk;
static int saw_sd;
static int cur_ci[MAXSEC];
static int sec_rel[MAXSEC];
static char sec_name[MAXSEC][MAXN];
/* Named section numbers are local to a module. 0x45 and 0x85 are both
 * section 5; the high bits are the class and the S3 text is the name. */
static char mod_sec_name[MAXMOD][MAXSEC][MAXN];
#define MAXR 8
#define MAXREQ 32
typedef struct {
    int lo, hi, at;
} range_t;
static range_t builtin_r[MAXSEC][MAXR];
static int builtin_n[MAXSEC];
typedef struct {
    char name[MAXN];
    range_t r[MAXR];
    int n;
} namemap_t;
static namemap_t namemap[MAXREQ];
static int nnamemap;
static int any_section;
static int cur_mod;

static char exe_directory[512];
static int have_exe_dir;

static void parse_stream(FILE *f, const char *path);

static uint8_t image[65536];
static uint8_t written[65536];
/* E0 holes are PC-relative: value = symbol + addend - hole address. */
static uint8_t pcrel[65536];

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [-v] [-nh] [-map mapfile] [userfile ...] [-d dirfile] [-l libfile ...] [-o absfile]\n",
        argv0);
    exit(1);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void copy_name(char *dst, const uint8_t *s, int n) {
    int i = 0;
    for (; i < n && i < MAXN - 1; i++) dst[i] = (char)s[i];
    dst[i] = 0;
}

static void parse_stream(FILE *f, const char *path) {
    for (int i = 0; i < MAXSEC; i++) {
        cur_ci[i] = -1;
        sec_rel[i] = 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != 'S' && *p != 's') continue;
        int typ = hexval(p[1]);
        if (typ < 0) continue;
        int len = (hexval(p[2]) << 4) | hexval(p[3]);
        if (len < 0) die("bad record in %s", path);
        int alen = 2;
        /* S3 is the 24-bit section-name record. S2/S7/S8 are 24-bit too. */
        if (typ == 2 || typ == 7 || typ == 8 || typ == 3) alen = 3;
        int dlen = len - alen - 1;
        if (dlen < 0 || dlen > 64) die("bad length in %s", path);
        const char *h = p + 4;
        uint32_t addr = 0;
        uint8_t raw[80];
        int ri = 0;
        raw[ri++] = (uint8_t)len;
        for (int i = 0; i < alen; i++) {
            int v = (hexval(h[0]) << 4) | hexval(h[1]);
            if (v < 0) die("bad hex in %s", path);
            addr = (addr << 8) | (uint32_t)v;
            raw[ri++] = (uint8_t)v;
            h += 2;
        }
        uint8_t data[64];
        for (int i = 0; i < dlen; i++) {
            int v = (hexval(h[0]) << 4) | hexval(h[1]);
            if (v < 0) die("bad hex in %s", path);
            data[i] = (uint8_t)v;
            raw[ri++] = (uint8_t)v;
            h += 2;
        }
        int cks = (hexval(h[0]) << 4) | hexval(h[1]);
        if (cks < 0 || ((srec_checksum(raw, ri) & 0xFF) != (cks & 0xFF)))
            die("checksum error in %s", path);

        if (typ == 3 && dlen > 0) {
            /* Low 6 bits are the module-local section. 0x40+n is the
             * DSCT form and 0x80+n is the PSCT form of the same n. */
            int id = (int)(addr & 0x3F);
            if (id >= 0 && id < MAXSEC && cur_mod >= 0 && cur_mod < MAXMOD) {
                int nlen = dlen < MAXN - 1 ? dlen : MAXN - 1;
                memcpy(mod_sec_name[cur_mod][id], data, (size_t)nlen);
                mod_sec_name[cur_mod][id][nlen] = 0;
                if (!sec_name[id][0]) {
                    memcpy(sec_name[id], data, (size_t)nlen);
                    sec_name[id][nlen] = 0;
                }
            }
        } else if (typ == 4 && ncon < MAXC) {
            contrib_t *c = &contribs[ncon];
            memset(c, 0, sizeof(*c));
            c->sec = data[0];
            int end = (data[1] << 8) | data[2];
            int rel = (c->sec >= 0 && c->sec < MAXSEC) ? sec_rel[c->sec] : 0;
            c->mod = cur_mod;
            if (addr == 0) {
                c->size = end + 1;
                c->abs = 0;
                c->start = 0;
                if (c->sec >= 0 && c->sec < MAXSEC) sec_rel[c->sec] = 1;
            } else if (rel) {
                /* Later span of a relative section. Address is the start offset. */
                c->size = (end - (int)addr + 1) & 0xFFFF;
                c->abs = 0;
                c->start = (int)addr;
            } else {
                c->org = (int)addr;
                c->size = (end - (int)addr + 1) & 0xFFFF;
                c->abs = 1;
                c->base = c->org;
                c->start = (int)addr;
            }
            if (c->sec >= 0 && c->sec < MAXSEC) cur_ci[c->sec] = ncon;
            ncon++;
        } else if (typ == 8 && nsym < MAXS) {
            sym_t *s = &symbols[nsym++];
            memset(s, 0, sizeof(*s));
            s->off = (int)((addr >> 8) & 0xFFFF);
            s->sec = (int)(addr & 0xFF);
            if (dlen >= 2 && data[0] == 0 && data[1] == 0)
                copy_name(s->name, data + 2, dlen - 2);
            else
                copy_name(s->name, data, dlen);
            s->ci = (s->sec >= 0 && s->sec < MAXSEC) ? cur_ci[s->sec] : -1;
            s->mod = cur_mod;
        } else if (typ == 7 && nfix < MAXF) {
            fix_t *fx = &fixups[nfix++];
            memset(fx, 0, sizeof(*fx));
            fx->off = (int)((addr >> 8) & 0xFFFF);
            fx->sec = (int)(addr & 0xFF);
            copy_name(fx->name, data, dlen);
            fx->ci = (fx->sec >= 0 && fx->sec < MAXSEC) ? cur_ci[fx->sec] : -1;
            fx->mod = cur_mod;
        } else if (typ == 2 && nchk < MAXK) {
            chunk_t *ch = &chunks[nchk++];
            memset(ch, 0, sizeof(*ch));
            ch->off = (int)((addr >> 8) & 0xFFFF);
            ch->sec = (int)(addr & 0xFF);
            ch->len = dlen;
            memcpy(ch->data, data, (size_t)dlen);
            ch->ci = (ch->sec >= 0 && ch->sec < MAXSEC) ? cur_ci[ch->sec] : -1;
            ch->mod = cur_mod;
        } else if (typ == 13) {
            saw_sd = 1;
        }
    }
}

static void parse_path(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die("can't open %s", path);
    parse_stream(f, path);
    fclose(f);
    cur_mod++;
}

static void w8(int addr, int b) {
    addr &= 0xFFFF;
    image[addr] = (uint8_t)b;
    written[addr] = 1;
}

static void w16(int addr, int v) {
    w8(addr, (v >> 8) & 0xFF);
    w8(addr + 1, v & 0xFF);
}

static int sec_base_of(int sec) {
    for (int i = 0; i < ncon; i++) if (contribs[i].sec == sec) return contribs[i].base;
    return 0;
}

static int mod_has_sec(int mod, int sec) {
    for (int i = 0; i < ncon; i++)
        if (contribs[i].mod == mod && contribs[i].sec == sec) return 1;
    return 0;
}

/* Section offset markers are relative to the emitting module's own base. */
static int mod_base(int mod, int sec) {
    for (int i = 0; i < ncon; i++) {
        if (contribs[i].mod != mod || contribs[i].sec != sec || contribs[i].abs) continue;
        if (contribs[i].start == 0) return contribs[i].base;
    }
    for (int i = 0; i < ncon; i++)
        if (contribs[i].mod == mod && contribs[i].sec == sec) return contribs[i].base;
    return 0;
}

/* C0 is an absolute external. E0 is a PC-relative external (LBSR/BSR).
 * C1..CF are section offsets only when that section exists in this module,
 * so an immediate opcode (C6 = LDB, and the rest of that family) stays a
 * literal in any other module. */
static int is_marker(int b, int mod) {
    if (b == 0xC0 || b == 0xE0) return 1;
    int sec = b - 0xC0;
    if (sec < 1 || sec > 15) return 0;
    return mod_has_sec(mod, sec);
}

/* (0x20|sec) is an 8-bit direct reloc. Section 0 is excluded: 0x20 is BRA. */
static int is_d8(int b, int mod) {
    if ((b & 0xF0) != 0x20 || (b & 0x0F) == 0) return 0;
    return mod_has_sec(mod, b & 0x0F);
}

static void scan(const uint8_t *data, int len, int i, int pc, int mod) {
    while (i < len) {
        int b = data[i];
        int d8 = is_d8(b, mod);
        if ((is_marker(b, mod) || d8) && i + 2 < len) {
            if (d8) {
                int off = (data[i + 1] << 8) | data[i + 2];
                int val = (mod_base(mod, b & 0x0F) + off) & 0xFF;
                w8(pc, val);
                pc = (pc + 1) & 0xFFFF;
            } else if (b == 0xC0 || b == 0xE0) {
                /* Two-byte addend. S7 adds the external symbol to it.
                 * E0 then subtracts the hole address (PC-relative). */
                w16(pc, (data[i + 1] << 8) | data[i + 2]);
                if (b == 0xE0) pcrel[pc] = 1;
                pc = (pc + 2) & 0xFFFF;
            } else {
                int off = (data[i + 1] << 8) | data[i + 2];
                int val = (mod_base(mod, b - 0xC0) + off) & 0xFFFF;
                w16(pc, val);
                pc = (pc + 2) & 0xFFFF;
            }
            i += 3;
            if (i >= len) break;
            /* Back-to-back markers (an address table) have no count. */
            if ((is_marker(data[i], mod) || is_d8(data[i], mod)) && i + 2 < len) continue;
            int n = data[i++];
            for (int k = 0; k < n && i < len; k++) {
                w8(pc, data[i++]);
                pc = (pc + 1) & 0xFFFF;
            }
            if (i >= len) break;
            /* The byte after the shield is an opcode, never a marker. */
            w8(pc, data[i++]);
            pc = (pc + 1) & 0xFFFF;
        } else {
            w8(pc, data[i]);
            pc = (pc + 1) & 0xFFFF;
            i++;
        }
    }
}

static void decode_chunk(const chunk_t *ch) {
    if (ch->ci < 0 || ch->len <= 0) return;
    contrib_t *c = &contribs[ch->ci];
    int dest = c->abs ? ch->off : (c->base + ch->off) & 0xFFFF;
    const uint8_t *d = ch->data;
    if ((is_marker(d[0], ch->mod) || is_d8(d[0], ch->mod)) && ch->len >= 3) {
        scan(d, ch->len, 0, dest, ch->mod);
        return;
    }
    /* Byte 0 counts leading raw literals. Marker-like opcodes sit in
     * there so the scan resumes on a real marker or a safe opcode. */
    int n = d[0];
    if (n > ch->len - 1) n = ch->len - 1;
    int pc = dest;
    for (int k = 0; k < n; k++) {
        w8(pc, d[1 + k]);
        pc = (pc + 1) & 0xFFFF;
    }
    scan(d, ch->len, 1 + n, pc, ch->mod);
}

static sym_t *find_sym(const char *name) {
    for (int i = 0; i < nsym; i++) if (strcmp(symbols[i].name, name) == 0) return &symbols[i];
    return NULL;
}

static int sym_addr(const sym_t *s) {
    if (!s) return 0;
    if (s->ci >= 0) {
        contrib_t *c = &contribs[s->ci];
        if (c->abs) return s->off & 0xFFFF;
        return (c->base + s->off) & 0xFFFF;
    }
    for (int i = 0; i < ncon; i++) {
        if (contribs[i].mod != s->mod || contribs[i].sec != s->sec) continue;
        if (contribs[i].abs) return s->off & 0xFFFF;
        return (contribs[i].base + s->off) & 0xFFFF;
    }
    /* No contribution: an absolute EQU (stdin, EOF) stores its value
     * in the offset and names section 0 without an ASCT span. */
    return s->off & 0xFFFF;
}

static int ref_addr(int ci, int mod, int sec, int off) {
    int use = ci;
    if (use < 0) {
        for (int i = 0; i < ncon; i++) {
            if (contribs[i].mod != mod || contribs[i].sec != sec) continue;
            if (!contribs[i].abs && contribs[i].start == 0) { use = i; break; }
        }
    }
    if (use < 0 || use >= ncon) return -1;
    contrib_t *c = &contribs[use];
    if (c->abs) return off & 0xFFFF;
    return (c->base + off) & 0xFFFF;
}

static int builtin_id(const char *w) {
    if (!strcasecmp(w, "ASCT")) return 0;
    if (!strcasecmp(w, "DSCT")) return 1;
    if (!strcasecmp(w, "PSCT")) return 2;
    if (!strcasecmp(w, "BSCT")) return 3;
    if (!strcasecmp(w, "CSCT")) return 4;
    return -1;
}

static int section_total(int sec) {
    int n = 0;
    for (int i = 0; i < ncon; i++)
        if (contribs[i].sec == sec && !contribs[i].abs) n += contribs[i].size;
    return n;
}

static int origin_of(int mod, int sec) {
    for (int i = 0; i < ncon; i++) {
        if (contribs[i].mod == mod && contribs[i].sec == sec &&
            !contribs[i].abs && contribs[i].start == 0)
            return contribs[i].base;
    }
    return -1;
}

static int block_extent(int mod, int sec) {
    int ext = 0;
    for (int i = 0; i < ncon; i++) {
        if (contribs[i].mod != mod || contribs[i].sec != sec || contribs[i].abs)
            continue;
        int end = contribs[i].start + contribs[i].size;
        if (end > ext) ext = end;
    }
    return ext;
}

static void assign_block(int mod, int sec, int base) {
    for (int i = 0; i < ncon; i++) {
        if (contribs[i].mod != mod || contribs[i].sec != sec || contribs[i].abs)
            continue;
        contribs[i].base = base;
    }
}

/* First range that can hold the whole module. A module that does not fit
 * stays for a later range, so a smaller later module can still fill the hole. */
static int take_range(range_t *r, int n, int ext) {
    if (ext < 0) ext = 0;
    for (int i = 0; i < n; i++) {
        int last = r[i].at + (ext ? ext - 1 : 0);
        if (ext == 0 || last <= r[i].hi) {
            int at = r[i].at;
            r[i].at += ext;
            return at;
        }
    }
    if (n <= 0) return -1;
    int at = r[n - 1].at;
    r[n - 1].at += ext;
    return at;
}

static int named_index(int mod, int sec) {
    if (sec < 5 || mod < 0 || mod >= MAXMOD) return -1;
    const char *nm = mod_sec_name[mod][sec];
    if (!nm[0]) return -1;
    for (int i = 0; i < nnamemap; i++)
        if (strcasecmp(namemap[i].name, nm) == 0) return i;
    return -1;
}

static int named_bytes(const char *nm) {
    int n = 0;
    for (int i = 0; i < ncon; i++) {
        contrib_t *c = &contribs[i];
        if (c->abs || c->sec < 5 || c->mod < 0 || c->mod >= MAXMOD) continue;
        if (strcasecmp(mod_sec_name[c->mod][c->sec], nm) == 0) n += c->size;
    }
    return n;
}

static void place(void) {
    if (!any_section) {
        int cursor = 0x100;
        for (int i = 0; i < ncon; i++) {
            contrib_t *c = &contribs[i];
            if (c->abs) c->base = c->org;
            else if (c->sec == 3 && c->size <= 256) c->base = 0;
            else {
                c->base = cursor;
                cursor += c->size;
            }
        }
        return;
    }
    /* One window packs from its start, same as a single SECTION base.
     * Two or more windows first-fit whole modules in directive order. */
    for (int s = 0; s < MAXSEC; s++)
        for (int k = 0; k < builtin_n[s]; k++) builtin_r[s][k].at = builtin_r[s][k].lo;
    for (int i = 0; i < nnamemap; i++)
        for (int k = 0; k < namemap[i].n; k++) namemap[i].r[k].at = namemap[i].r[k].lo;

    int inited_b[MAXSEC], pack_b[MAXSEC];
    int inited_n[MAXREQ], pack_n[MAXREQ];
    unsigned char multi[MAXMOD][MAXSEC];
    int next_fb = 0x100;
    memset(inited_b, 0, sizeof(inited_b));
    memset(inited_n, 0, sizeof(inited_n));
    memset(multi, 0, sizeof(multi));
    for (int i = 0; i < ncon; i++) {
        contrib_t *c = &contribs[i];
        if (c->abs) { c->base = c->org; continue; }
        int sec = c->sec;
        int mod = c->mod;
        if (sec < 0 || sec >= MAXSEC || mod < 0 || mod >= MAXMOD) continue;
        int ni = named_index(mod, sec);
        int nr = (ni >= 0) ? namemap[ni].n : builtin_n[sec];
        if (nr >= 2) {
            if (multi[mod][sec]) continue;
            multi[mod][sec] = 1;
            range_t *r = (ni >= 0) ? namemap[ni].r : builtin_r[sec];
            assign_block(mod, sec, take_range(r, nr, block_extent(mod, sec)));
            continue;
        }
        if (nr == 1) {
            int *pack = (ni >= 0) ? &pack_n[ni] : &pack_b[sec];
            int *inited = (ni >= 0) ? &inited_n[ni] : &inited_b[sec];
            int base0 = (ni >= 0) ? namemap[ni].r[0].lo : builtin_r[sec][0].lo;
            if (!*inited) { *pack = base0; *inited = 1; }
            if (c->start == 0) c->base = *pack;
            else {
                int org = origin_of(mod, sec);
                c->base = org >= 0 ? org : *pack;
            }
            *pack += c->size;
            continue;
        }
        if (ni >= 0) {
            if (!inited_n[ni]) {
                pack_n[ni] = next_fb;
                next_fb += named_bytes(namemap[ni].name);
                inited_n[ni] = 1;
            }
            if (c->start == 0) c->base = pack_n[ni];
            else {
                int org = origin_of(mod, sec);
                c->base = org >= 0 ? org : pack_n[ni];
            }
            pack_n[ni] += c->size;
            continue;
        }
        if (!inited_b[sec]) {
            if (sec == 3 && section_total(sec) <= 256) pack_b[sec] = 0;
            else {
                pack_b[sec] = next_fb;
                next_fb += section_total(sec);
            }
            inited_b[sec] = 1;
        }
        if (c->start == 0) c->base = pack_b[sec];
        else {
            int org = origin_of(mod, sec);
            c->base = org >= 0 ? org : pack_b[sec];
        }
        pack_b[sec] += c->size;
    }
}

static int entry_point(void) {
    if (written[0xFFFE] && written[0xFFFF]) {
        int vec = (image[0xFFFE] << 8) | image[0xFFFF];
        for (int i = 0; i < ncon; i++) {
            if (contribs[i].sec != 2) continue;
            int loc = (contribs[i].base + (contribs[i].abs ? 0 : contribs[i].start)) & 0xFFFF;
            int sz = contribs[i].size;
            if (sz > 0 && vec >= loc && vec < loc + sz) return vec;
        }
    }
    for (int i = 0; i < nsym; i++) {
        if (symbols[i].sec == 2) return sym_addr(&symbols[i]);
    }
    return sec_base_of(2);
}

static void emit_image(FILE *fo, int entry) {
    int a = 0;
    while (a < 65536) {
        if (!written[a]) { a++; continue; }
        int start = a;
        uint8_t buf[32];
        int n = 0;
        while (a < 65536 && written[a] && n < 32) buf[n++] = image[a++];
        srec_emit(fo, SREC_S1, (uint32_t)start, buf, n);
    }
    if (saw_sd) {
        uint8_t d[2] = { 0x00, 0x08 };
        srec_emit(fo, SREC_SD, (uint32_t)entry, d, 2);
    }
    srec_emit(fo, SREC_S9, 0, NULL, 0);
}

static int size_of(int sec) {
    int n = 0;
    for (int i = 0; i < ncon; i++) if (contribs[i].sec == sec) n += contribs[i].size;
    return n;
}

static void write_map(FILE *fm, int entry) {
    fprintf(fm, "\nBinary Systems, Inc. Linker, Version %s\n", VERSION);
    fprintf(fm, "%s\n\n\n", COPYRIGHT);
    fprintf(fm, " Section Size Summary\n");
    fprintf(fm, " --------------------\n");
    fprintf(fm, " ASCT = %04X\n", size_of(0));
    fprintf(fm, " BSCT = %04X\n", size_of(3));
    fprintf(fm, " DSCT = %04X\n", size_of(1));
    fprintf(fm, " PSCT = %04X\n", size_of(2));
    fprintf(fm, "\n\n     Entry Point at: %04X\n\n", entry);
    fprintf(fm, " UNSATISFIED EXTERNAL REFERENCES\n\n");
    fprintf(fm, " Symbol    Referenced From\n");
    fprintf(fm, " --------  ---------------\n");
    for (int i = 0; i < nfix; i++) {
        if (find_sym(fixups[i].name)) continue;
        int addr = ref_addr(fixups[i].ci, fixups[i].mod, fixups[i].sec, fixups[i].off);
        if (addr < 0) continue;
        fprintf(fm, " %-8s  %04X\n", fixups[i].name, addr);
    }
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

static void dir_dirname(const char *path, char *out, size_t n) {
    snprintf(out, n, "%s", path);
    char *sl = strrchr(out, '/');
    if (!sl) { snprintf(out, n, "."); return; }
    if (sl == out) { snprintf(out, n, "/"); return; }
    *sl = 0;
}

static int resolve_path(const char *spec, const char *hint, int library, char *out, size_t nout) {
    if (spec[0] && access(spec, R_OK) == 0) {
        snprintf(out, nout, "%s", spec);
        return 1;
    }
    const char *base = base_of(spec);
    if (base[0] && access(base, R_OK) == 0) {
        snprintf(out, nout, "%s", base);
        return 1;
    }
    if (scan_dir_ci(".", base, out, nout)) return 1;
    if (hint && hint[0]) {
        char joined[512];
        snprintf(joined, sizeof(joined), "%s/%s", hint, base);
        if (access(joined, R_OK) == 0) {
            snprintf(out, nout, "%s", joined);
            return 1;
        }
        if (scan_dir_ci(hint, base, out, nout)) return 1;
    }
    if (library && have_exe_dir) {
        char joined[512], real[512];
        /* Shipped with the tools. A local WINTEK checkout is only a fallback. */
        snprintf(joined, sizeof(joined), "%s/../lib", exe_directory);
        if (realpath(joined, real) && scan_dir_ci(real, base, out, nout)) return 1;
        snprintf(joined, sizeof(joined), "%s/../WINTEK", exe_directory);
        if (realpath(joined, real) && scan_dir_ci(real, base, out, nout)) return 1;
    }
    return 0;
}

#define MAXLIBS 16
#define MAXDIRF 64
#define MAXLIBMOD 128

typedef struct {
    int named;
    char name[MAXN];
    int id;
    int lo, hi;
} secreq_t;

typedef struct {
    const char *text;
    size_t len;
    int used;
} libmod_t;

static int dir_words(char *line, char words[][64], int maxw) {
    int n = 0;
    char *p = line;
    while (*p && n < maxw) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        int i = 0;
        while (*p && !isspace((unsigned char)*p) && i < 63) words[n][i++] = *p++;
        words[n][i] = 0;
        while (*p && !isspace((unsigned char)*p)) p++;
        n++;
    }
    return n;
}

static int parse_span(const char *s, int *lo, int *hi) {
    if (*s == '$') s++;
    char *end = NULL;
    long a = strtol(s, &end, 16);
    if (end == s) return 0;
    *lo = (int)a & 0xFFFF;
    if (*end == '-') {
        if (end[1] == '$') end++;
        char *e2 = NULL;
        long b = strtol(end + 1, &e2, 16);
        if (e2 == end + 1) *hi = 0xFFFF;
        else *hi = (int)b & 0xFFFF;
    } else {
        *hi = 0xFFFF;
    }
    return 1;
}

static void read_dir(const char *path, char files[][512], int *nfiles,
                     char libs[][512], int *nlibs, secreq_t *reqs, int *nreqs) {
    FILE *f = fopen(path, "r");
    if (!f) die("can't open %s", path);
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == 0 || *p == '*' || *p == ';' || *p == '\r') continue;
        size_t n = strlen(p);
        while (n && (p[n - 1] == '\n' || p[n - 1] == '\r')) p[--n] = 0;
        char words[8][64];
        int nw = dir_words(p, words, 8);
        if (nw <= 0) continue;
        if (!strcasecmp(words[0], "END")) break;
        if (!strcasecmp(words[0], "FILE") && nw >= 2) {
            if (*nfiles >= MAXDIRF) die("too many FILE lines");
            snprintf(files[*nfiles], 512, "%s", words[1]);
            (*nfiles)++;
        } else if (!strcasecmp(words[0], "LIBRARY") && nw >= 2) {
            if (*nlibs >= MAXLIBS) die("too many libraries");
            snprintf(libs[*nlibs], 512, "%s", words[1]);
            (*nlibs)++;
        } else if (!strcasecmp(words[0], "SECTION")) {
            int at = -1;
            for (int i = 1; i < nw; i++) if (strcmp(words[i], "@") == 0) at = i;
            if (at < 2 || at + 1 >= nw) continue;
            if (*nreqs >= MAXREQ) die("too many sections");
            int type_id = builtin_id(words[at - 1]);
            int name_upto = (type_id >= 0) ? at - 1 : at;
            secreq_t *r = &reqs[*nreqs];
            memset(r, 0, sizeof(*r));
            if (!parse_span(words[at + 1], &r->lo, &r->hi)) continue;
            if (name_upto > 1) {
                r->named = 1;
                snprintf(r->name, sizeof(r->name), "%s", words[1]);
                r->id = -1;
            } else if (type_id >= 0) {
                r->named = 0;
                r->id = type_id;
            } else continue;
            (*nreqs)++;
        }
    }
    fclose(f);
}

static int name_in_objects(const char *nm) {
    for (int m = 0; m < MAXMOD; m++)
        for (int s = 5; s < MAXSEC; s++)
            if (mod_sec_name[m][s][0] && strcasecmp(mod_sec_name[m][s], nm) == 0)
                return 1;
    return 0;
}

static void add_named_range(const char *nm, int lo, int hi) {
    int k = -1;
    for (int i = 0; i < nnamemap; i++)
        if (strcasecmp(namemap[i].name, nm) == 0) { k = i; break; }
    if (k < 0) {
        if (nnamemap >= MAXREQ) die("too many sections");
        k = nnamemap++;
        memset(&namemap[k], 0, sizeof(namemap[k]));
        snprintf(namemap[k].name, sizeof(namemap[k].name), "%s", nm);
    }
    if (namemap[k].n >= MAXR) die("too many ranges");
    namemap[k].r[namemap[k].n].lo = lo;
    namemap[k].r[namemap[k].n].hi = hi;
    namemap[k].n++;
}

static void bind_sections(const secreq_t *reqs, int nreqs) {
    if (nreqs <= 0) return;
    any_section = 1;
    for (int i = 0; i < nreqs; i++) {
        if (reqs[i].named) {
            if (!name_in_objects(reqs[i].name))
                warn("section %s not found in objects", reqs[i].name);
            add_named_range(reqs[i].name, reqs[i].lo, reqs[i].hi);
            continue;
        }
        int id = reqs[i].id;
        if (id < 0 || id >= MAXSEC) continue;
        if (builtin_n[id] >= MAXR) die("too many ranges");
        builtin_r[id][builtin_n[id]].lo = reqs[i].lo;
        builtin_r[id][builtin_n[id]].hi = reqs[i].hi;
        builtin_n[id]++;
    }
}

static void symbol_name(const uint8_t *data, int n, char *out) {
    if (n >= 2 && data[0] == 0 && data[1] == 0) copy_name(out, data + 2, n - 2);
    else copy_name(out, data, n);
}

static int srec_fields(const char *line, int *typ, uint8_t *data, int *dlen) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if ((*p != 'S' && *p != 's') || !p[1]) return 0;
    int t = hexval(p[1]);
    if (t < 0) return 0;
    if (hexval(p[2]) < 0 || hexval(p[3]) < 0) return 0;
    int len = (hexval(p[2]) << 4) | hexval(p[3]);
    int alen = (t == 2 || t == 3 || t == 7 || t == 8) ? 3 : 2;
    int n = len - alen - 1;
    if (n < 0 || n > 64) return 0;
    const char *h = p + 4;
    for (int i = 0; i < alen; i++) {
        if (hexval(h[0]) < 0 || hexval(h[1]) < 0) return 0;
        h += 2;
    }
    for (int i = 0; i < n; i++) {
        int v = (hexval(h[0]) << 4) | hexval(h[1]);
        if (v < 0) return 0;
        data[i] = (uint8_t)v;
        h += 2;
    }
    *typ = t;
    *dlen = n;
    return 1;
}

static int module_wanted(const char *text, size_t len) {
    const char *p = text;
    const char *end = text + len;
    char line[512];
    while (p < end) {
        const char *ls = p;
        while (p < end && *p != '\n') p++;
        size_t n = (size_t)(p - ls);
        if (p < end && *p == '\n') p++;
        if (n && ls[n - 1] == '\r') n--;
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, ls, n);
        line[n] = 0;
        int typ, dlen;
        uint8_t data[80];
        if (!srec_fields(line, &typ, data, &dlen) || typ != 8) continue;
        char name[MAXN];
        symbol_name(data, dlen, name);
        if (!name[0] || find_sym(name)) continue;
        for (int i = 0; i < nfix; i++)
            if (strcmp(fixups[i].name, name) == 0) return 1;
    }
    return 0;
}

static void parse_memory(const char *text, size_t len, const char *label) {
    char *tmp = malloc(len + 1);
    if (!tmp) die("out of memory");
    memcpy(tmp, text, len);
    tmp[len] = 0;
    FILE *f = fmemopen(tmp, len, "r");
    if (!f) { free(tmp); die("can't read %s", label); }
    parse_stream(f, label);
    fclose(f);
    free(tmp);
    cur_mod++;
}

static int split_library(char *buf, size_t n, libmod_t *mods, int maxm) {
    const char *p = buf;
    const char *end = buf + n;
    const char *start = NULL;
    int count = 0;
    while (p < end) {
        const char *ls = p;
        while (p < end && *p != '\n') p++;
        size_t len = (size_t)(p - ls);
        const char *q = ls;
        while (q < ls + len && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
        int s0 = (q + 1 < ls + len && (*q == 'S' || *q == 's') && q[1] == '0');
        int s9 = (q + 1 < ls + len && (*q == 'S' || *q == 's') && q[1] == '9');
        if (p < end && *p == '\n') p++;
        if (s0 && !start) start = ls;
        if (s9 && start) {
            if (count < maxm) {
                mods[count].text = start;
                mods[count].len = (size_t)(p - start);
                mods[count].used = 0;
                count++;
            }
            start = NULL;
        }
    }
    return count;
}

static char *read_all(const char *path, size_t *nout) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || sz > 2 * 1024 * 1024) { fclose(f); die("library too large: %s", path); }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) die("out of memory");
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    *nout = got;
    return buf;
}

static void pull_library(const char *path) {
    size_t n = 0;
    char *buf = read_all(path, &n);
    if (!buf) { warn("can't open library %s", path); return; }
    libmod_t *mods = calloc(MAXLIBMOD, sizeof(libmod_t));
    if (!mods) die("out of memory");
    int nmod = split_library(buf, n, mods, MAXLIBMOD);
    int guard = 0, progressed = 1;
    while (progressed && guard++ < nmod + 1) {
        progressed = 0;
        for (int i = 0; i < nmod; i++) {
            if (mods[i].used) continue;
            if (!module_wanted(mods[i].text, mods[i].len)) continue;
            mods[i].used = 1;
            parse_memory(mods[i].text, mods[i].len, path);
            progressed = 1;
        }
    }
    free(mods);
    free(buf);
}

int main(int argc, char **argv) {
    int verbose = 0, noheader = 0;
    const char *mapfile = NULL;
    const char *outfile = NULL;
    const char *dirfile = NULL;
    const char **users = calloc((size_t)argc, sizeof(char *));
    const char *clibs[MAXLIBS];
    int nuser = 0, nclib = 0;
    if (!users) die("out of memory");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else if (strcmp(argv[i], "-nh") == 0) noheader = 1;
        else if (strcmp(argv[i], "-map") == 0 && i + 1 < argc) mapfile = argv[++i];
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) dirfile = argv[++i];
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            if (nclib < MAXLIBS) clibs[nclib++] = argv[++i];
            else die("too many libraries");
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outfile = argv[++i];
        else if (argv[i][0] == '-') usage(argv[0]);
        else users[nuser++] = argv[i];
    }
    init_exe_dir();
    if (!noheader) banner("Binary Systems, Inc. Linker", VERSION, COPYRIGHT);

    char dir_files[MAXDIRF][512];
    char dir_libs[MAXLIBS][512];
    secreq_t reqs[MAXREQ];
    int ndf = 0, ndl = 0, nreq = 0;
    char hint[512];
    snprintf(hint, sizeof(hint), ".");
    if (dirfile) {
        dir_dirname(dirfile, hint, sizeof(hint));
        read_dir(dirfile, dir_files, &ndf, dir_libs, &ndl, reqs, &nreq);
    }
    if (nuser == 0 && ndf == 0) {
        fprintf(stderr, "no input files\n");
        free(users);
        return 1;
    }
    for (int i = 0; i < nuser; i++) parse_path(users[i]);
    for (int i = 0; i < ndf; i++) {
        char resolved[512];
        if (!resolve_path(dir_files[i], hint, 0, resolved, sizeof(resolved)))
            die("can't open %s", dir_files[i]);
        parse_path(resolved);
    }
    bind_sections(reqs, nreq);

    for (int i = 0; i < nclib; i++) {
        char resolved[512];
        if (!resolve_path(clibs[i], hint, 1, resolved, sizeof(resolved)))
            warn("can't open library %s", clibs[i]);
        else
            pull_library(resolved);
    }
    for (int i = 0; i < ndl; i++) {
        char resolved[512];
        if (!resolve_path(dir_libs[i], hint, 1, resolved, sizeof(resolved)))
            warn("can't open library %s", dir_libs[i]);
        else
            pull_library(resolved);
    }

    place();
    for (int i = 0; i < nchk; i++) decode_chunk(&chunks[i]);

    for (int i = 0; i < nfix; i++) {
        int addr = ref_addr(fixups[i].ci, fixups[i].mod, fixups[i].sec, fixups[i].off);
        if (addr < 0) continue;
        sym_t *s = find_sym(fixups[i].name);
        int addend = written[addr] ? ((image[addr] << 8) | image[addr + 1]) : 0;
        int val = s ? (sym_addr(s) + addend) & 0xFFFF : 0;
        if (s && pcrel[addr]) val = (val - addr) & 0xFFFF;
        w16(addr, val);
    }
    int entry = entry_point();

    const char *out = outfile ? outfile : "a.mik";
    FILE *fo = fopen(out, "w");
    if (!fo) die("can't open output file");
    emit_image(fo, entry);
    fclose(fo);

    if (mapfile) {
        FILE *fm = fopen(mapfile, "w");
        if (!fm) die("can't open map file");
        write_map(fm, entry);
        fclose(fm);
    }
    if (verbose) {
        printf("         output file: %s\n", out);
        printf("entry %04X\n", entry);
    }
    if (verbose || !noheader) {
        printf("Link complete -> %s (absolute S-record)\n", out);
        if (mapfile) printf("Map: %s\n", mapfile);
    }
    free(users);
    return 0;
}
