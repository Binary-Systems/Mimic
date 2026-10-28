	NAM     chario
	TTL     'Character I/O Routines'

	INCL    'IOloc.h'
	STL     'ACIA routines'

	PSCT
	GLOBAL  getchar,putchar,putc,getc
	EXTERN  .LINK

	SPC     5,15
***     getchar - read 1 character from the 6809 Control Module's ACIA 1
*
*            This routine is written to be compatable with the standard
*       "getchar".  It changes a carriage return character into a C '\n'
*       character.  Hence, both a line feed and a carriage return read
*       as a newline.  The standard "getchar" also returns the value EOF
*       (usually -1) on the end of file.  This "getchar" therefore
*       returns a -1 when a CONTROL-D is read.
**
**      C calling sequence:  c = getchar();
**
**              where: c = (int) character read except that
**                         '\r' mapped into '\n' and control-D
**                         reads as -1.

getchar SUBA    #0        no parameters are expected
	BEQ     getchr0
	JSR     .LINK

*   We call the assembly language routine INCH which waits for a
*   character then returns it in B.

getchr0 BSR     INCH      call assembly language INCH routine

*   We clear A in preparation for the return.  Next, we change '\r'
*   into '\n'.  Note that we echo a '\r' as a '\r', '\n' sequence.

	CLRA
	CMPB    #0Dh
	BNE     getchr1
	BSR     OUTCH
	LDB     #0Ah

*   Here we echo the character and then change a CONTROL-D into the
*   value -1. (The constant 04 hex is a control-D).

getchr1 BSR     OUTCH     echo the character
	CMPB    #04h
	BNE     getchr2
	LDD     #-1

*   Finally, we set the 'Z' bit to reflect our return value and return

getchr2 TSTB
	RTS

	SPC     5,15
***     getc - read 1 character from a file/channel
*
*            "Getc" is typically used to perform all input in a
*       conventional C environment.  I.e., all input functions
*       eventually call "getc" as the primitive input function.
*
*            In a stand-alone environment, "getc" must be customized for
*       the particular I/O devices supplied.  This "getc" function
*       ignores its argument and calls the "getchar" function given
*       above.
**
**      C calling sequence: getc(c);
**
**              where: fp = (int) file pointer (ignored)

getc    SUBA    #2      expect 2 parameter bytes,
	BEQ     getc0
	JSR     .LINK

*   Just call "getchar" to read a character from the ACIA

getc0   CLRA
	BSR     getchar

*   Now clean-up the stack and return.

	PULS    X
	LEAS    2,S
	JMP     0,X     getchar has set 'Z' bit,

	SPC     5,15
***     putchar - write 1 character
*
*            This routine is designed to be compatible with the standard
*       "putchar".  It therefore changes a C '\n' character into the
*       sequence "line feed", "carriage return".
**
**      C calling sequence:  putchar(c);
**
**              where: c = (char) character to be written

putchar SUBA    #2      expect 2 parameter bytes,
	BEQ     putchr0
	JSR     .LINK

*   Here we get the character into B.

putchr0 LDB     3,S

*   Next, we change a '\n' into the two character sequence '\n\r'.

	CMPB    #0Ah
	BNE     putchr1
	BSR     OUTCH     call assembly language routine
	LDB     #0Dh

*   Here we output the character or the '\r' of the two character
*   sequence above.

putchr1 BSR     OUTCH

*   Finally, we return the character we just wrote.  Note that the
*   character is below the return address on the stack so we can't
*   use a simple RTS to return.

	PULS    X
	PULS    D         (AB) = parameter,
	SUBD    #0        set the 'Z' bit according
	JMP     0,X        to the returned value,

	SPC     5,15
***     putc - write 1 character to a file/channel
*
*            "Putc" is typically used to perform all output in a
*       conventional C environment.  I.e., all output functions
*       eventually call "putc" as the primitive output function.
*
*            In a stand-alone environment, "putc" must be customized for
*       the particular I/O devices supplied.
*
*            This "putc" function ignores the second argument and calls
*       the "putchar" function given above.  Therefore, the "printf"
*       routine (in the runtime package) which calls "putc" will write
*       to the console ACIA.
**
**      C calling sequence: putc(c, fp);
**
**              where: c = (char) character to write
**
**                     fp = (int) file pointer (ignored)

putc    SUBA    #4      expect 4 parameter bytes,
	BEQ     putc0
	JSR     .LINK

*   Here we call "putchar" to write the character to the ACIA.

putc0   LDD     4,S
	PSHS    D
	LDA     #2
	BSR     putchar

*   Now we clean-up the stack and return.

	PULS    X
	LEAS    4,S
	JMP     0,X     putchar has set 'Z' bit,

	SPC     5,15
***     INCH - Input 1 character from ACIA
*

INCH    LDB     ACIA1SR get Rx status
	LSRB
	BCC     INCH
	LDB     ACIA1DR get data
	ANDB    #$7F    mask down to 7 bits,
	RTS

	SPC     5,15
***     OUTCH - Output 1 character to ACIA
*
*       ENTRY: (B) = ASCII value of character
*
*       EXIT: (B) = ASCII value
*
*       USES: CCR except 'C' bit

OUTCH   PSHS    A
	LDA     #2      mask for ACIASR
OUTCH1  BITA    ACIA1SR
	BEQ     OUTCH1
	STB     ACIA1DR send (B) to ACIA
	PULS    A
	RTS

	END
