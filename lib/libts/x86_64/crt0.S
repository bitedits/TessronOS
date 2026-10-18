/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	crt0.S (x86-64)
 *	Entry of a TessronOS program (design 9.6).
 *
 *	The kernel starts the main task at _start with an empty stack in
 *	the process's own space, as if _start had been called. main() runs
 *	and its return value ends the process, so _start never returns.
 */

	.text
	.global	_start
	.type	_start, @function
_start:
	xor	%ebp, %ebp			/* end the frame chain */

	/* clear .bss */
	lea	__bss_start(%rip), %rdi
	lea	__bss_end(%rip), %rcx
	sub	%rdi, %rcx
	xor	%eax, %eax
	rep stosb

	and	$-16, %rsp			/* aligned for the call */
	call	main
	mov	%eax, %edi			/* the exit code, no return */
	call	ts_ext_prc
1:	jmp	1b
	.size	_start, . - _start

	.section .note.GNU-stack, "", @progbits
