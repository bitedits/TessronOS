/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpudef.h
 *	CPU-dependent definition (x86-64)
 */

#ifndef __TK_CPUDEF_CORE_H__
#define __TK_CPUDEF_CORE_H__

/*
 * Coprocessor attribute: the x87, SSE and AVX state is treated as
 * coprocessor 0.
 */
#if USE_FPU
#define	TA_COPS		TA_COP0
#else
#define	TA_COPS		0
#endif
#define TA_FPU		TA_COP0

/*
 * General purpose register		tk_get_reg tk_set_reg
 */
typedef struct t_regs {
	VD	rax, rbx, rcx, rdx, rsi, rdi, rbp;
	VD	r8, r9, r10, r11, r12, r13, r14, r15;
} T_REGS;

/*
 * Exception-related register		tk_get_reg tk_set_reg
 */
typedef struct t_eit {
	void	*pc;		/* return address (rip) */
	UD	rflags;
	UW	taskmode;	/* Task mode flag */
} T_EIT;

/*
 * Control register			tk_get_reg tk_set_reg
 */
typedef struct t_cregs {
	void	*ssp;		/* System stack pointer */
	void	*usp;		/* user stack pointer (rsp of a task at protection level 2/3) */
} T_CREGS;

/*
 * Coprocessor register			tk_get_cpr tk_set_cpr
 */
#if NUM_COPROCESSOR > 0
typedef struct t_copregs {
	UB	fxsave[512];	/* the FXSAVE image: x87, MXCSR, xmm0-15 */
} T_COPREGS;
#endif  /* NUM_COPROCESSOR  */

#endif /* __TK_CPUDEF_CORE_H__ */
