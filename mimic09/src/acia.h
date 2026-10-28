/* acia.h — 6850 ACIA emulation */

#ifndef MIMIC09_ACIA_H
#define MIMIC09_ACIA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void acia_init(uint16_t base);
uint8_t acia_read(uint16_t off);
void acia_write(uint16_t off, uint8_t val);

/* Terminal / input helpers for use during G runs (safe to call even without ACIA) */
void acia_poll_input(void);
void acia_end_run(void);

#ifdef __cplusplus
}
#endif
#endif
