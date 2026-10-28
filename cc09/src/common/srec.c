/* srec.c - minimal but usable relocatable S-record support (text, CRLF)
 * See docs/reloc-srecords.md for format notes from reverse engineering.
 * This is a starting point; expand as A9/XLD are implemented.
 */

#include "srec.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    return -1;
}

FILE *srec_open_read(const char *path) {
    FILE *f = fopen(path, "r");
    return f;
}

int srec_read(FILE *f, srec_t *out) {
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        char *p = buf;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != 'S' && *p != 's') continue;   /* tolerate junk / CRLF */
        p++;
        int typ = hexval(*p++);
        if (typ < 0) continue;
        int len = (hexval(p[0]) << 4) | hexval(p[1]); p += 2;
        if (len < 0 || len > 255) return -1;
        out->type = (srec_type_t)typ;
        out->len = len - 1;  /* rough; payload after addr varies */
        /* parse addr depending on type (simplified) */
        uint32_t addr = 0;
        int alen = 2;
        if (typ == 2 || typ == 8 || typ == 7) alen = 3; /* 24 bit variants in use */
        if (typ == 3 || typ == 7) alen = 4;
        if (typ == 0 || typ == 1 || typ == 5 || typ == 9 || typ == 4 || typ == 13) alen = 2;
        for (int i = 0; i < alen && *p; i++) {
            int v = hexval(p[0]) * 16 + hexval(p[1]);
            if (v < 0) return -1;
            addr = (addr << 8) | (uint8_t)v;
            p += 2;
        }
        out->addr = addr;
        int dlen = len - 1 - alen;   /* approx */
        if (dlen < 0) dlen = 0;
        if (dlen > (int)sizeof(out->data)) dlen = sizeof(out->data);
        out->len = dlen;
        for (int i = 0; i < dlen; i++) {
            int v = (hexval(p[0]) << 4) | hexval(p[1]);
            if (v < 0) return -1;
            out->data[i] = (uint8_t)v;
            p += 2;
        }
        /* checksum at p (2 hex) - we ignore strict for bootstrap */
        return 1;
    }
    return 0; /* EOF */
}

uint8_t srec_checksum(const uint8_t *bytes, int n) {
    uint8_t sum = 0;
    for (int i = 0; i < n; i++) sum += bytes[i];
    return (uint8_t)~sum;
}

int srec_emit(FILE *f, srec_type_t typ, uint32_t addr, const uint8_t *data, int dlen) {
    if (!f) return -1;
    int alen = 2;
    /* S3 is the 24-bit section-name record (address = id + 0x40, data = name). */
    if (typ == 2 || typ == 8 || typ == 7 || typ == 3) alen = 3;
    int len = alen + 1 + dlen;  /* count byte value: addr + data + checksum */
    if (dlen < 0 || len > 255) return -1;
    uint8_t tmp[260];
    if (1 + alen + dlen > (int)sizeof(tmp)) return -1;
    int ti = 0;
    tmp[ti++] = (uint8_t)len;
    for (int i = alen - 1; i >= 0; i--) tmp[ti++] = (addr >> (8 * i)) & 0xff;
    for (int i = 0; i < dlen; i++) tmp[ti++] = data[i];
    uint8_t cks = srec_checksum(tmp, ti);
    fprintf(f, "S%1X%02X", (int)typ, len);
    for (int i = 0; i < alen; i++) fprintf(f, "%02X", (addr >> (8 * (alen - 1 - i))) & 0xff);
    for (int i = 0; i < dlen; i++) fprintf(f, "%02X", data[i]);
    fprintf(f, "%02X\r\n", cks);
    return 0;
}

int srec_write(FILE *f, const srec_t *rec) {
    return srec_emit(f, rec->type, rec->addr, rec->data, rec->len);
}

/* Simple writer state for assembler emission of reloc S-rec objects */
struct srec_writer {
    FILE *f;
    char module[64];
};

srec_writer_t *srec_writer_open(const char *path, const char *modulename) {
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    srec_writer_t *w = (srec_writer_t *)calloc(1, sizeof(*w));
    w->f = f;
    strncpy(w->module, modulename ? modulename : "module", sizeof(w->module)-1);
    return w;
}

void srec_writer_close(srec_writer_t *w) {
    if (!w) return;
    if (w->f) fclose(w->f);
    free(w);
}

void srec_w_header(srec_writer_t *w, const char *name) {
    if (!w || !w->f) return;
    uint8_t d[32];
    int n = 0;
    const char *nm = name ? name : w->module;
    for (; *nm && n < 30; nm++) d[n++] = (uint8_t)*nm;
    srec_emit(w->f, SREC_S0, 0, d, n);
}

void srec_w_section_meta(srec_writer_t *w, int tag, uint16_t val1, uint16_t val2) {
    if (!w || !w->f) return;
    uint8_t d[4];
    d[0] = (uint8_t)tag;
    d[1] = (val1 >> 8) & 0xff; d[2] = val1 & 0xff;
    d[3] = (val2 >> 8) & 0xff; /* may be truncated by caller for 3-byte metas */
    srec_emit(w->f, SREC_S4, 0, d, 3);
}

void srec_w_symbol(srec_writer_t *w, const char *name, int section_tag, uint16_t local_off, int flags) {
    if (!w || !w->f || !name) return;
    uint8_t d[64];
    int n = 0;
    d[n++] = (uint8_t)section_tag;
    d[n++] = (local_off >> 8) & 0xff;
    d[n++] = local_off & 0xff;
    for (const char *p = name; *p && n < 60; p++) d[n++] = (uint8_t)*p;
    /* flags could be encoded; for now we just emit basic form observed */
    srec_emit(w->f, SREC_S8, 0, d, n);
}

void srec_w_data_chunk(srec_writer_t *w, int section_tag, uint16_t chunk_off, const uint8_t *bytes, int n) {
    if (!w || !w->f) return;
    uint8_t buf[256];
    int m = 0;
    buf[m++] = (uint8_t)section_tag;
    buf[m++] = (chunk_off >> 8) & 0xff;
    buf[m++] = chunk_off & 0xff;
    for (int i = 0; i < n && m < 250; i++) buf[m++] = bytes[i];
    /* Use S2 for 16-bit chunks (common in samples) */
    srec_emit(w->f, SREC_S2, 0, buf, m);
}

void srec_w_chunk_end(srec_writer_t *w, const char *name, int section_tag, uint16_t info) {
    if (!w || !w->f) return;
    uint8_t d[32];
    int n = 0;
    d[n++] = (uint8_t)section_tag;
    for (const char *p = name ? name : ""; *p && n < 28; p++) d[n++] = (uint8_t)*p;
    srec_emit(w->f, SREC_S7, info, d, n);
}

void srec_w_trailer(srec_writer_t *w, int tag, uint16_t info) {
    if (!w || !w->f) return;
    uint8_t d[2] = { (uint8_t)tag, (uint8_t)info };
    srec_emit(w->f, (srec_type_t)13 /*SD*/, 0, d, 2);
}

void srec_w_end(srec_writer_t *w, uint16_t entry) {
    if (!w || !w->f) return;
    srec_emit(w->f, SREC_S9, entry, NULL, 0);
}
