/* util.c */

#include "util.h"
#include <ctype.h>
#include <stdlib.h>

int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

int parse_hex_byte(const char *p, uint8_t *out)
{
    int h = hexval(p[0]);
    int l = hexval(p[1]);
    if (h < 0 || l < 0) return 0;
    *out = (uint8_t)((h << 4) | l);
    return 1;
}

int parse_hex_word(const char *p, uint16_t *out)
{
    uint8_t hi, lo;
    if (!parse_hex_byte(p, &hi) || !parse_hex_byte(p+2, &lo)) return 0;
    *out = (uint16_t)((hi << 8) | lo);
    return 1;
}

int parse_radix(const char *s, unsigned long *out, const char **end)
{
    while (*s && isspace((unsigned char)*s)) s++;
    int base = 16;
    if (*s == '$') { base = 16; s++; }
    else if (*s == '+') { base = 10; s++; }
    else if (*s == '&') { base = 8; s++; }
    else if (*s == '%') { base = 2; s++; }
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (!*s) return 0;
    char *e = NULL;
    unsigned long v = strtoul(s, &e, base);
    if (e == s) return 0;
    *out = v;
    if (end) *end = e;
    return 1;
}
