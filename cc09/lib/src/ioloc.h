	STL     'I/O locations'

*       This file defines the locations of the ACIAs and PIAs.

	ASCT

**      The first ACIA is device U8.

ACIA1CR EQU     $F940   ACIA control register
ACIA1SR EQU     $F940   ACIA status register
ACIA1DR EQU     $F941   ACIA data register

**      The second ACIA is device U9.

ACIA2CR EQU     $F980   ACIA control register
ACIA2SR EQU     $F980   ACIA status register
ACIA2DR EQU     $F981   ACIA data register

**      The first PIA is device U7.

PIA1DRA EQU     $FB00   PIA-1, data I/O register A
PIA1DRB EQU     $FB01   PIA-1, data I/O register B
PIA1CRA EQU     $FB02   PIA-1, control register A
PIA1CRB EQU     $FB03   PIA-1, control register B

**      The second PIA is device U6.

PIA2DRA EQU     $FA80   PIA-2, data I/O register A
PIA2DRB EQU     $FA81   PIA-2, data I/O register B
PIA2CRA EQU     $FA82   PIA-2, control register A
PIA2CRB EQU     $FA83   PIA-2, control register B

	STL     ''
