	TTL     'A C Start-Up Routine'
	NAM     Cstart
	GLOBAL  .CSTRT
	EXTERN  stdin
	EXTERN  stdout
	EXTERN  EOF
	EXTERN  main,.clrio

	DSCT
*
*   This section reserves memory for the stack area.  Note that the
*   stack is placed in DSCT.  This normally corresponds to RAM.  The
*   number "256" is the number of bytes reserved.  C programs
*   require extensive stack space since all automatic variables are
*   allocated storage on the stack.  Programs which have large
*   amounts of automatic storage or involve deep recursion may
*   require more stack space than is allocated here.

	RMB     256     Reserve memory
stack   EQU     *       for the stack

	PSCT

*   These instructions are placed in PSCT since this normally
*   corresponds to ROM.  Here we load the stack pointer, initialize
*   the I/O system, set the actual number of parameters to zero, and
*   call routine "main".  If we return from main we simply loop back
*   and do everything again.

.CSTRT  LDS     #stack  load stack pointer,
	JSR     .clrio  reset I/O system,
	CLRA            no parameters, (A) = 0
	JSR     main
	JMP     .CSTRT

	ASCT

*   This section loads the RESET vector with the
*   address of this start-up routine.

	ORG     $FFFE
	FDB     .CSTRT  at the startup routine,

	END     .CSTRT
