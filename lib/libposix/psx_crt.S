/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_crt.S
 *	Entry of a POSIX program (lib/libposix, design 5.8.3)
 *
 *	_start clears .bss and hands over to __psx_start (psx_start.c),
 *	which never returns. __psx_on_stack(sp, fn, arg) calls fn(arg) on
 *	another stack: the program's own, larger than the one the kernel
 *	gives the main task.
 */

	.text
	.balign	4

	.global	_start
	.type	_start, %function
_start:
	mov	x29, #0
	mov	x30, #0
	adrp	x0, __bss_start
	add	x0, x0, :lo12:__bss_start
	adrp	x1, __bss_end
	add	x1, x1, :lo12:__bss_end
1:	cmp	x0, x1
	b.hs	2f
	str	xzr, [x0], #8
	b	1b
2:
	bl	__psx_start
3:	b	3b
	.size	_start, . - _start

/*
 * The entry a caught signal is handed to (include/ts/psx.h): x0 the
 * picture of where the task was, x1 the signal, sp just below the
 * picture. x19 to x28 are not in the picture, and the handler, a C
 * function, would leave them changed when the task goes back without
 * returning through it; they are kept here round it, and ts_sig_ret
 * goes back with them as they were.
 */
	.global	__psx_sigentry
	.type	__psx_sigentry, %function
__psx_sigentry:
	sub	sp, sp, #96
	stp	x19, x20, [sp, #0]
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	stp	x25, x26, [sp, #48]
	stp	x27, x28, [sp, #64]
	str	x0, [sp, #80]
	mov	x29, #0
	bl	__psx_sighandle
	ldr	x0, [sp, #80]
	ldp	x19, x20, [sp, #0]
	ldp	x21, x22, [sp, #16]
	ldp	x23, x24, [sp, #32]
	ldp	x25, x26, [sp, #48]
	ldp	x27, x28, [sp, #64]
	b	ts_sig_ret
	.size	__psx_sigentry, . - __psx_sigentry

	.global	__psx_on_stack
	.type	__psx_on_stack, %function
__psx_on_stack:
	mov	sp, x0
	mov	x29, #0
	mov	x30, #0
	mov	x0, x2
	blr	x1
4:	b	4b
	.size	__psx_on_stack, . - __psx_on_stack
