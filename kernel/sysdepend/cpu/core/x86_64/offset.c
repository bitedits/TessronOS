/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	offset.c (x86-64)
 *	Generates offset.h (structure offsets used by the assembler sources).
 *	The makefile compiles this file with -S and extracts the "@@" lines.
 */

#include <sys/machine.h>
#include <tk/tkernel.h>
#include "kernel.h"
#include "cpu_task.h"

#define DEFINE(sym, val)	Asm("\n@@" #sym " %c0" : : "i"((BINT)(val)))

void knl_offset_generator( void )
{
	DEFINE(TCB_tskid,	offsetof(TCB, tskid));
	DEFINE(TCB_tskatr,	offsetof(TCB, tskatr));
	DEFINE(TCB_tskctxb,	offsetof(TCB, tskctxb));
	DEFINE(TCB_state,	offsetof(TCB, state));
	DEFINE(TCB_isstack,	offsetof(TCB, isstack));
	DEFINE(TCB_runprc,	offsetof(TCB, runprc));
	DEFINE(PCPU_prcid,	offsetof(T_PCPU, prcid));
	DEFINE(PCPU_ctxtsk,	offsetof(T_PCPU, ctxtsk));
	DEFINE(PCPU_schedtsk,	offsetof(T_PCPU, schedtsk));
	DEFINE(PCPU_dispatch_disabled,	offsetof(T_PCPU, dispatch_disabled));
	DEFINE(PCPU_taskindp,	offsetof(T_PCPU, taskindp));
	DEFINE(PCPU_cur_ttbr0,	offsetof(T_PCPU, cur_ttbr0));
	DEFINE(PCPU_tmp_stack,	offsetof(T_PCPU, tmp_stack));
	DEFINE(PCPU_irq_stack,	offsetof(T_PCPU, irq_stack));
	DEFINE(PCPU_ctxsw_cnt,	offsetof(T_PCPU, ctxsw_cnt));
	DEFINE(PCPU_user_rsp,	offsetof(T_PCPU, user_rsp));
	DEFINE(PCPU_kstack,	offsetof(T_PCPU, kstack));
	DEFINE(PCPU_tss_rsp0,	offsetof(T_PCPU, tss) + offsetof(T_TSS, rsp0));
	DEFINE(CTXB_ssp,	offsetof(CTXB, ssp));
	DEFINE(CTXB_ttbr0,	offsetof(CTXB, ttbr0));
	DEFINE(CTXB_fsbase,	offsetof(CTXB, fsbase));
	DEFINE(SSF_SIZE,	sizeof(SStackFrame));
	DEFINE(FR_rax,		offsetof(T_EXCFRAME, rax));
	DEFINE(FR_vec,		offsetof(T_EXCFRAME, vec));
	DEFINE(FR_err,		offsetof(T_EXCFRAME, err));
	DEFINE(FR_rip,		offsetof(T_EXCFRAME, rip));
	DEFINE(FR_cs,		offsetof(T_EXCFRAME, cs));
	DEFINE(FR_rflags,	offsetof(T_EXCFRAME, rflags));
	DEFINE(FR_rsp,		offsetof(T_EXCFRAME, rsp));
}
