* acia-hello.asm
* Minimal self-contained 6809 test program.
* Writes a short banner to the ACIA (addresses from original ioloc.h: $F940/$F941)
* Uses polled output similar to CHARIO.ASM's OUTCH.
* Intended to be assembled with a9 + linked xld for .mik (the mission "compiles via cc09").
*
* Assembled by a9 and linked by xld. The image is what mimic09 runs.
* SYNC returns control to the host so -g can finish after the banner.
*
* This exercises the ACIA emulation <-> terminal/pipe path for the Mimic mission.
* Future: link with chario.rel + cstart.rel + C-produced main using printf (via putc).

	NAM     aciahello
	TTL     'ACIA Hello Test for Mimic'

ACIA1SR	EQU	$F940
ACIA1DR	EQU	$F941

	PSCT
start
	LDS	#stack
	; Write "HI!\r\n" using polled OUTCH
	LDB	#'H'
	BSR	OUTCH
	LDB	#'I'
	BSR	OUTCH
	LDB	#'!'
	BSR	OUTCH
	LDB	#$0D
	BSR	OUTCH
	LDB	#$0A
	BSR	OUTCH
	; SYNC halts so mimic09 -g returns after the banner is written
spin
	SYNC

* Polled output char in B to ACIA1 (matches chario/ioloc)
OUTCH
	PSHS	A
	LDA	#2		; TDRE mask (bit 1)
OUTCH1
	BITA	ACIA1SR
	BEQ	OUTCH1
	STB	ACIA1DR
	PULS	A
	RTS

	DSCT
	RMB	128
stack	EQU	*

	ASCT
	ORG	$FFFE
	FDB	start

	END	start
