/* hex.c - Wintek PROM Image Filter.
 * PROM Image Filter, V1.0r3. Copyright 2026 Binary Systems, Inc.
 *
 * hex [options] first last <input >output
 * Addresses are hexadecimal (a leading $ is accepted).
 * Reads S1/S2/S3 and Intel Hex into a 64K image. S0/S5/S9/SD are ignored.
 * Writes Intel Hex type 00 records of up to 16 data bytes, CRLF, for
 * every address in [first, last]. Unloaded bytes are 0xFF, the erased
 * PROM value, so a window matches the archived .HEX image. Always ends
 * with :00000001FF. -D inverts data. -S writes S1 records.
 * -W and -A are accepted and do not change the bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

static const char *BANNER1 = "     PROM Image Filter, V1.0r3";
static const char *BANNER2 = "Copyright 2026 Binary Systems, Inc.";

static uint8_t image[65536];
static uint8_t written[65536];

static void banner(void) {
    fprintf(stderr, "%s\n%s\n", BANNER1, BANNER2);
}

static void usage(void) {
    banner();
    fprintf(stderr, "Usage: hex [options] first last <input >output\n");
    fprintf(stderr, "Available options are:\n");
    fprintf(stderr, "  -W for Wintek PROM pinout\n");
    fprintf(stderr, "  -A for inverting address buffers\n");
    fprintf(stderr, "  -D for inverting data buffers\n");
    fprintf(stderr, "  -S for S-record format output\n");
    exit(1);
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int parse_addr(const char *s) {
    if (!s || !*s) return -1;
    if (*s == '$') s++;
    char *end = NULL;
    long v = strtol(s, &end, 16);
    if (end == s || *end || v < 0 || v > 0xFFFF) return -1;
    return (int)v;
}

static void note(int addr, int b) {
    if (addr < 0 || addr > 0xFFFF) return;
    image[addr] = (uint8_t)b;
    written[addr] = 1;
}

static int load_srec(const char *line) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if ((*p != 'S' && *p != 's') || !p[1]) return 0;
    int typ = hexval((unsigned char)p[1]);
    if (typ < 0) return 0;
    /* S0 header, S5 count, S9 end, SD summary: do not write their data. */
    if (typ == 0 || typ == 5 || typ == 9 || typ == 0xD) return 1;
    if (typ != 1 && typ != 2 && typ != 3) return 1;
    if (hexval((unsigned char)p[2]) < 0 || hexval((unsigned char)p[3]) < 0) return 0;
    int len = (hexval((unsigned char)p[2]) << 4) | hexval((unsigned char)p[3]);
    int alen = (typ == 1) ? 2 : 3;
    int dlen = len - alen - 1;
    if (dlen < 0 || dlen > 64) return 0;
    const char *h = p + 4;
    unsigned sum = (unsigned)len;
    uint32_t addr = 0;
    for (int i = 0; i < alen; i++) {
        int v = (hexval((unsigned char)h[0]) << 4) | hexval((unsigned char)h[1]);
        if (v < 0) return 0;
        addr = (addr << 8) | (uint32_t)v;
        sum += (unsigned)v;
        h += 2;
    }
    uint8_t data[64];
    for (int i = 0; i < dlen; i++) {
        int v = (hexval((unsigned char)h[0]) << 4) | hexval((unsigned char)h[1]);
        if (v < 0) return 0;
        data[i] = (uint8_t)v;
        sum += (unsigned)v;
        h += 2;
    }
    int cks = (hexval((unsigned char)h[0]) << 4) | hexval((unsigned char)h[1]);
    if (cks < 0 || ((~(int)sum) & 0xFF) != (cks & 0xFF)) {
        fprintf(stderr, "checksum error\n");
        return 0;
    }
    int a = (int)(addr & 0xFFFF);
    for (int i = 0; i < dlen && a < 65536; i++, a++) note(a, data[i]);
    return 1;
}

static int load_intel(const char *line) {
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return 0;
    p++;
    int nibs[256];
    int n = 0;
    while (p[0] && p[1] && n < 256) {
        int hi = hexval((unsigned char)p[0]);
        int lo = hexval((unsigned char)p[1]);
        if (hi < 0 || lo < 0) break;
        nibs[n++] = (hi << 4) | lo;
        p += 2;
    }
    if (n < 5) return 0;
    int count = nibs[0];
    if (n < 4 + count + 1) return 0;
    int sum = 0;
    for (int i = 0; i < 4 + count + 1; i++) sum += nibs[i];
    if ((sum & 0xFF) != 0) {
        fprintf(stderr, "checksum error\n");
        return 0;
    }
    int addr = (nibs[1] << 8) | nibs[2];
    int typ = nibs[3];
    if (typ == 1) return 2; /* EOF */
    if (typ != 0) return 1;
    for (int i = 0; i < count && addr < 65536; i++, addr++) note(addr, nibs[4 + i]);
    return 1;
}

static void emit_intel(int addr, const uint8_t *data, int n, int invert) {
    int sum = n + ((addr >> 8) & 0xFF) + (addr & 0xFF);
    printf(":%02X%04X00", n, addr & 0xFFFF);
    for (int i = 0; i < n; i++) {
        int b = data[i] & 0xFF;
        if (invert) b ^= 0xFF;
        printf("%02X", b);
        sum += b;
    }
    printf("%02X\r\n", (-sum) & 0xFF);
}

static void emit_s1(int addr, const uint8_t *data, int n, int invert) {
    int count = n + 3;
    int sum = count + ((addr >> 8) & 0xFF) + (addr & 0xFF);
    printf("S1%02X%04X", count, addr & 0xFFFF);
    for (int i = 0; i < n; i++) {
        int b = data[i] & 0xFF;
        if (invert) b ^= 0xFF;
        printf("%02X", b);
        sum += b;
    }
    printf("%02X\r\n", (~sum) & 0xFF);
}

static void emit_window(int first, int last, int as_srec, int invert) {
    int a = first;
    while (a <= last) {
        int start = a;
        uint8_t buf[16];
        int n = 0;
        while (a <= last && n < 16) {
            buf[n++] = written[a] ? image[a] : 0xFF;
            a++;
        }
        if (as_srec) emit_s1(start, buf, n, invert);
        else emit_intel(start, buf, n, invert);
    }
    if (as_srec) printf("S9030000FC\r\n");
    else printf(":00000001FF\r\n");
}

int main(int argc, char **argv) {
    int first = -1, last = -1;
    int opt_w = 0, opt_a = 0, opt_d = 0, opt_s = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-W") == 0) opt_w = 1;
        else if (strcmp(argv[i], "-A") == 0) opt_a = 1;
        else if (strcmp(argv[i], "-D") == 0) opt_d = 1;
        else if (strcmp(argv[i], "-S") == 0) opt_s = 1;
        else if (argv[i][0] == '-') usage();
        else if (first < 0) first = parse_addr(argv[i]);
        else if (last < 0) last = parse_addr(argv[i]);
        else usage();
    }
    (void)opt_w;
    (void)opt_a;
    if (first < 0 || last < 0 || first > last) {
        fprintf(stderr, "invalid address range\n");
        usage();
    }
    banner();

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == ':') {
            if (load_intel(p) == 2) break;
        } else if (*p == 'S' || *p == 's') {
            load_srec(p);
        }
    }
    emit_window(first, last, opt_s, opt_d);
    return 0;
}
