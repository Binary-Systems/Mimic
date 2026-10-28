	NAM     clrio
	TTL     'I/O Initialization Routines'

	INCL    'IOloc.h'
	STL     'ACIA Initialization'

	PSCT
	GLOBAL  cleario,.clrio
	EXTERN  .LINK

***     cleario, .clrio - reset the ACIA
*
*            This routine master resets the 6809 Control Module's ACIA.
*       It has a C entry point and an assembler entry point.
**
**      C calling sequence:  cleario();
**
**      Assembler calling sequence:
**
**              JSR     .clrio

cleario SUBA    #0      (C entry point)
	BEQ     .clrio
	JSR     .LINK

*   We store a 3 into the ACIA control register to perform the
*   master reset.

.clrio  LDB     #$03    (Assembler entry point)
	STB     ACIA1CR

*   The constant 15 (hex) sets-up the ACIA for 8 bits, no parity, 1
*   stop bits, divide by 16 (clock rate), with RTS = 0 and Tx
*   interrupts inhibited.

	LDB     #$15
	STB     ACIA1CR 8+N+1, 1/16, RTS true

*   We return an "int" zero as the default returned value in C.
*   Note that the 'Z' bit is set properly.  Since there are no
*   parameters we can use an RTS to return.

	CLRD        return (int) 0 with 'Z' bit set,
	RTS

	END
