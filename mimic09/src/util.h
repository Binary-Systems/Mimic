/* util.h — small helpers */

#ifndef MIMIC09_UTIL_H
#define MIMIC09_UTIL_H

#include <stdint.h>
#include <stddef.h>

int hexval(char c);
int parse_hex_byte(const char *p, uint8_t *out);
int parse_hex_word(const char *p, uint16_t *out);

/* $ hex, + decimal, & octal, % binary, 0x hex. No prefix means hex.
 * *end, when not NULL, receives the first unparsed character. */
int parse_radix(const char *s, unsigned long *out, const char **end);

#endif
