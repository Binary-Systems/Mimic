/* pia.h — 6821 PIA emulation (basic) */

#ifndef MIMIC09_PIA_H
#define MIMIC09_PIA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pia_init(uint16_t base);
uint8_t pia_read(uint16_t off);
void pia_write(uint16_t off, uint8_t val);

#ifdef __cplusplus
}
#endif
#endif
