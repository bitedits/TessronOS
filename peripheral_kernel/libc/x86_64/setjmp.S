/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	setjmp.S
 *	Leaving a deep call in one step (x86-64)
 *
 *	Saves exactly what the calling convention says a function must
 *	preserve for its caller: rbx, rbp, r12-r15, the stack pointer as the
 *	caller left it and the return address. Nothing else can be observed
 *	to have changed by the code that jumps back. The kernel is built
 *	with -mgeneral-regs-only, so no code that can reach here holds a
 *	value in the x87 or SSE registers.
 *
 *	jmp_buf (include/setjmp.h, 13 words): rbx, rbp, r12, r13, r14, r15,
 *	rsp, rip
 */

	.text
	.global	setjmp
	.type	setjmp, @function
setjmp:
	mov	%rbx, 0(%rdi)
	mov	%rbp, 8(%rdi)
	mov	%r12, 16(%rdi)
	mov	%r13, 24(%rdi)
	mov	%r14, 32(%rdi)
	mov	%r15, 40(%rdi)
	lea	8(%rsp), %rdx			/* the caller's stack pointer after the return */
	mov	%rdx, 48(%rdi)
	mov	(%rsp), %rdx			/* the return address */
	mov	%rdx, 56(%rdi)
	xor	%eax, %eax
	ret
	.size	setjmp, . - setjmp

	.global	longjmp
	.type	longjmp, @function
longjmp:
	mov	%esi, %eax
	test	%eax, %eax
	jnz	1f
	inc	%eax				/* setjmp returns 1 for a value of 0 */
1:	mov	0(%rdi), %rbx
	mov	8(%rdi), %rbp
	mov	16(%rdi), %r12
	mov	24(%rdi), %r13
	mov	32(%rdi), %r14
	mov	40(%rdi), %r15
	mov	48(%rdi), %rsp
	jmp	*56(%rdi)
	.size	longjmp, . - longjmp

	.section .note.GNU-stack, "", @progbits
