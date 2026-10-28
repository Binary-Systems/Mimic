/* loader.c — robust S-Record + Intel HEX + raw binary */

#include "loader.h"
#include "cpu.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static uint8_t checksum(const uint8_t *buf, int len)
{
    uint8_t s = 0;
    for (int i = 0; i < len; i++) s += buf[i];
    return (~s) & 0xFF;
}

static int load_srec(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int total = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
        if (*p != 'S' && *p != 's') continue;
        char type = (char)toupper((unsigned char)p[1]);
        /* Data records only. S0/S5/S7/S8/S9/SD must not be written as code. */
        if (type != '1' && type != '2' && type != '3') continue;
        int len = 0;
        unsigned addr = 0;
        int alen = (type == '1' || type == '9') ? 2 : (type == '2' || type == '8') ? 3 : 4;
        if (sscanf(p+2, "%2x", &len) != 1) continue;
        const char *addrp = p + 4;
        if (alen == 2) sscanf(addrp, "%4x", &addr);
        else if (alen == 3) sscanf(addrp, "%6x", &addr);
        else sscanf(addrp, "%8x", &addr);
        int data_start = 4 + alen * 2;
        int data_len = len - alen - 1;
        if (data_len < 0) continue;
        for (int i = 0; i < data_len; i++) {
            unsigned b = 0;
            if (sscanf(p + data_start + i*2, "%2x", &b) != 1) break;
            mem_write8((uint16_t)(addr + i), (uint8_t)b);
            total++;
        }
        if (type == '7' || type == '8' || type == '9') {
            /* termination with optional entry point; monitor user sets PC */
        }
    }
    fclose(f);
    return total;
}

static int load_ihex(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int total = 0;
    uint32_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        /* tolerate CRLF, leading whitespace, \r only lines */
        char *p = line;
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
        if (*p != ':') continue;
        p++; /* skip : */
        unsigned len=0, addr=0, type=0;
        if (sscanf(p, "%2x%4x%2x", &len, &addr, &type) != 3) continue;
        if (type == 0) { /* data */
            uint16_t a = (uint16_t)(base + addr);
            char *datahex = p + 6; /* after len(2) addr(4) type(2) */
            for (unsigned i = 0; i < len; i++) {
                unsigned b = 0;
                if (sscanf(datahex + i*2, "%2x", &b) != 1) break;
                mem_write8((uint16_t)(a + i), (uint8_t)b);
                total++;
            }
        } else if (type == 1) { /* eof */
            break;
        } else if (type == 2) { /* extended segment */
            unsigned seg = 0; sscanf(p + 6, "%4x", &seg); base = seg * 16U;
        } else if (type == 4) { /* extended linear */
            unsigned seg = 0; sscanf(p + 6, "%4x", &seg); base = (uint32_t)seg << 16;
        }
    }
    fclose(f);
    return total;
}

static int load_raw(const char *path, uint16_t at)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int total = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        mem_write8(at++, (uint8_t)c);
        total++;
    }
    fclose(f);
    return total;
}

int load_file(const char *path, uint16_t force_addr, int force)
{
    /* auto detect by content or extension */
    const char *ext = strrchr(path, '.');
    if (force || (ext && (strcasecmp(ext, ".bin") == 0 || strcasecmp(ext, ".rom") == 0))) {
        return load_raw(path, force_addr);
    }
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    /* Skip leading CR/LF/whitespace to find real first significant char */
    int first = 0;
    for (;;) {
        int c = fgetc(f);
        if (c == EOF) break;
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
            first = c;
            break;
        }
    }
    fclose(f);
    if (first == ':') return load_ihex(path);
    if (first == 'S' || first == 's') return load_srec(path);
    if (force_addr) return load_raw(path, force_addr);
    return load_raw(path, 0);
}

int save_file(const char *path, uint16_t start, uint16_t end, const char *fmt)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int count = 0;
    if (strcasecmp(fmt, "BIN") == 0) {
        for (uint16_t a = start; a <= end; a++) {
            fputc(mem_read8(a), f);
            count++;
        }
    } else if (strcasecmp(fmt, "IHEX") == 0 || strcasecmp(fmt, "HEX") == 0) {
        uint16_t addr = start;
        while (addr <= end) {
            int len = (end - addr + 1) > 16 ? 16 : (end - addr + 1);
            fprintf(f, ":%02X%04X00", len, addr);
            uint8_t sum = len + (addr >> 8) + (addr & 0xFF) + 0;
            for (int i = 0; i < len; i++) {
                uint8_t b = mem_read8((uint16_t)(addr + i));
                fprintf(f, "%02X", b);
                sum += b;
            }
            fprintf(f, "%02X\n", (uint8_t)((~sum + 1) & 0xFF));
            addr += (uint16_t)len;
            count += len;
        }
        fprintf(f, ":00000001FF\n");
    } else { /* S19 default */
        uint16_t addr = start;
        while (addr <= end) {
            int len = (end - addr + 1) > 32 ? 32 : (end - addr + 1);
            uint8_t rec[3 + 32 + 1];
            rec[0] = (uint8_t)(len + 3); /* count */
            rec[1] = addr >> 8;
            rec[2] = addr & 0xFF;
            for (int i = 0; i < len; i++) rec[3 + i] = mem_read8((uint16_t)(addr + i));
            uint8_t cs = checksum(rec, len + 3);
            fprintf(f, "S1%02X%04X", len + 3, addr);
            for (int i = 0; i < len; i++) fprintf(f, "%02X", rec[3 + i]);
            fprintf(f, "%02X\n", cs);
            addr += (uint16_t)len;
            count += len;
        }
        /* termination record */
        uint16_t exec = start;
        fprintf(f, "S903%04X%02X\n", exec, (uint8_t)(~ (3 + (exec>>8) + (exec&0xFF)) & 0xFF));
    }
    fclose(f);
    return count;
}
