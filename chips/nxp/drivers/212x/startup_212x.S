/* Reset and exception entry for LPC2000.
 *
 * WHAT IS DIFFERENT FROM THE USUAL LPC2000 STARTUP. The IRQ vector here does
 * NOT do the classic `ldr pc,[pc,#-0xFF0]` -- branching straight to whatever
 * VICVectAddr holds. That trick is two instructions shorter, but it makes the
 * VIC's target an EXCEPTION handler: it must be ARM mode and carry
 * __attribute__((interrupt("IRQ"))) so it returns with `subs pc, lr, #4`.
 *
 * Everything else in this port is Thumb, and a handler that has to be ARM and
 * specially attributed is a handler you cannot write in ordinary C. So the
 * vector lands in a trampoline that saves what an AAPCS call may clobber,
 * calls Chip_VIC_Dispatch() as a normal function, and returns properly. Costs
 * about a dozen cycles per interrupt and buys plain C handlers.
 */

	.equ  MODE_FIQ, 0x11
	.equ  MODE_IRQ, 0x12
	.equ  MODE_SVC, 0x13
	.equ  MODE_ABT, 0x17
	.equ  MODE_UND, 0x1B
	.equ  MODE_SYS, 0x1F
	.equ  I_BIT,    0x80
	.equ  F_BIT,    0x40

#ifndef IRQ_STACK_SIZE
#define IRQ_STACK_SIZE  0x100
#endif
#ifndef FIQ_STACK_SIZE
#define FIQ_STACK_SIZE  0x40
#endif
#ifndef ABT_STACK_SIZE
#define ABT_STACK_SIZE  0x20
#endif
#ifndef UND_STACK_SIZE
#define UND_STACK_SIZE  0x20
#endif
#ifndef SVC_STACK_SIZE
#define SVC_STACK_SIZE  0x40
#endif
/* The boot ROM's IAP workspace: the top 32 bytes of on-chip RAM. Not a stack
 * and not ours -- just space nothing else may occupy. */
#ifndef IAP_RESERVE
#define IAP_RESERVE     0x20
#endif

	.section .vectors, "ax"
	.code 32
	.align 2
	.global _vectors
_vectors:
	ldr  pc, =_start                /* 0x00 reset                       */
	ldr  pc, =_undef                /* 0x04 undefined instruction       */
	ldr  pc, =_swi                  /* 0x08 software interrupt          */
	ldr  pc, =_pabort               /* 0x0C prefetch abort              */
	ldr  pc, =_dabort               /* 0x10 data abort                  */
	.word 0x00000000                /* 0x14 CHECKSUM -- see below       */
	ldr  pc, =_irq_trampoline       /* 0x18 IRQ                         */
	ldr  pc, =_fiq                  /* 0x1C FIQ                         */

/* THE WORD AT 0x14 IS NOT RESERVED, it is a checksum, and getting it wrong is
 * the classic LPC2000 afternoon. The boot loader adds the eight vector words;
 * if the sum is not zero it decides the flash holds no valid program and
 * enters ISP instead -- the part comes up dead with no clue why.
 *
 * lpc21isp and most flash tools patch it as they write. A raw JTAG write does
 * not, so the zero above is a placeholder and the build has to fix it -- see
 * csp_lpc2000_checksum, which is the flash tool's job because that is where the
 * image is already in hand.
 *
 * Measured on this very table: with the placeholder the eight words sum to
 * FFFFFFF8, and the value that makes them sum to zero is B8A06F60. Off by
 * eight, and the part would have come up in ISP with no output at all.
 */

	.text
	.code 32
	.align 2

/* Exceptions we do not handle. They still STOP -- a debugger should find you at
 * the fault rather than three resets later -- but they say WHICH one first, by
 * blinking the boot LED forever.
 *
 * `b .` was silent, and silence here is indistinguishable from a hung loop, a
 * wedged peripheral or a dead console. An undefined instruction reached from
 * ordinary C code (an `mcr` to a coprocessor this part does not have, say)
 * then looks exactly like a UART that will not receive. Costing an evening.
 *
 * Each takes the number in r0 and does not return; csp_fault_blink loops.
 *   1 undefined instruction   3 prefetch abort   5 FIQ
 *   2 software interrupt      4 data abort
 */
_undef:  mov r0, #1
	 b   csp_fault_blink
_swi:    mov r0, #2
	 b   csp_fault_blink
_pabort: mov r0, #3
	 b   csp_fault_blink
_dabort: mov r0, #4
	 b   csp_fault_blink
_fiq:    mov r0, #5
	 b   csp_fault_blink

/* IRQ. lr_irq is return+4 on ARM7, so the -4 is the correction, and the SPSR
 * has to be saved because a nested interrupt would otherwise overwrite it.
 */
	.global _irq_trampoline
_irq_trampoline:
	sub    lr, lr, #4
	stmfd  sp!, {r0-r3, r12, lr}
	mrs    r0, spsr
	stmfd  sp!, {r0}
	bl     Chip_VIC_Dispatch
	ldmfd  sp!, {r0}
	msr    spsr_cxsf, r0
	ldmfd  sp!, {r0-r3, r12, pc}^   /* the ^ restores CPSR from SPSR    */

	.global _start
_start:
	/* A stack per mode, carved down from the top of RAM. Each `msr` puts
	 * us in that mode just long enough to load its banked sp.
	 *
	 * MINUS THE IAP RESERVATION FIRST. The flash routines in the boot ROM
	 * use the top 32 bytes of on-chip RAM as their own workspace -- they
	 * are called with the part's own stack, and they take that region
	 * regardless of what is in it. A stack placed there survives right up
	 * until the first flash write, which is a fault that only appears when
	 * you save something.
	 */
	ldr   r0, =_stack_top
	sub   r0, r0, #IAP_RESERVE

	msr   CPSR_c, #MODE_UND|I_BIT|F_BIT
	mov   sp, r0
	sub   r0, r0, #UND_STACK_SIZE

	msr   CPSR_c, #MODE_ABT|I_BIT|F_BIT
	mov   sp, r0
	sub   r0, r0, #ABT_STACK_SIZE

	msr   CPSR_c, #MODE_FIQ|I_BIT|F_BIT
	mov   sp, r0
	sub   r0, r0, #FIQ_STACK_SIZE

	msr   CPSR_c, #MODE_IRQ|I_BIT|F_BIT
	mov   sp, r0
	sub   r0, r0, #IRQ_STACK_SIZE

	msr   CPSR_c, #MODE_SVC|I_BIT|F_BIT
	mov   sp, r0
	sub   r0, r0, #SVC_STACK_SIZE

	/* System mode for the program itself: the same registers as User but
	 * privileged, so the runtime can touch the VIC without an SWI.
	 * Interrupts stay masked until main enables them.
	 */
	msr   CPSR_c, #MODE_SYS|I_BIT|F_BIT
	mov   sp, r0

	/* .data from flash to RAM. The linker gives us where it was stored
	 * (_data_load) and where it belongs.
	 */
	ldr   r1, =_data_load
	ldr   r2, =_data_start
	ldr   r3, =_data_end
1:	cmp   r2, r3
	ldrlo r0, [r1], #4
	strlo r0, [r2], #4
	blo   1b

	/* .bss to zero. C promises it, nothing else does it. */
	mov   r0, #0
	ldr   r2, =_bss_start
	ldr   r3, =_bss_end
2:	cmp   r2, r3
	strlo r0, [r2], #4
	blo   2b

	/* Clock, MAM and the VIC before any C runs that might use them. */
	bl    SystemInit

	mov   r0, #0                    /* argc */
	mov   r1, #0                    /* argv */
	ldr   r12, =main
	mov   lr, pc
	bx    r12                       /* bx: main may be Thumb            */

	/* main returned. There is nowhere to go. */
3:	b     3b

	.end
