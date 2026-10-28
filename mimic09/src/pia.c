/* pia.c — very basic 6821 PIA (two ports, no full handshake yet) */

#include "pia.h"
#include "cpu.h"

static uint16_t pia_base;
static uint8_t pra, ddra, cra;
static uint8_t prb, ddrb, crb;

void pia_init(uint16_t base)
{
    pia_base = base;
    pra = prb = 0;
    ddra = ddrb = 0;
    cra = crb = 0;
    cpu_register_device(base, pia_read, pia_write);
}

uint8_t pia_read(uint16_t off)
{
    switch (off & 3) {
    case 0: return (cra & 0x04) ? pra : ddra;
    case 1: return cra;
    case 2: return (crb & 0x04) ? prb : ddrb;
    case 3: return crb;
    }
    return 0xFF;
}

void pia_write(uint16_t off, uint8_t val)
{
    switch (off & 3) {
    case 0:
        if (cra & 0x04) pra = val; else ddra = val;
        break;
    case 1: cra = val; break;
    case 2:
        if (crb & 0x04) prb = val; else ddrb = val;
        break;
    case 3: crb = val; break;
    }
}
