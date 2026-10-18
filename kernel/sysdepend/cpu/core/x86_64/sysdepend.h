/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sysdepend.h
 *	System-dependent definition (x86-64 core)
 */

#ifndef _SYSDEPEND_CPU_CORE_SYSDEPEND_
#define _SYSDEPEND_CPU_CORE_SYSDEPEND_

#include "bootinfo.h"

/* Exception and interrupt entries (vector.S, dispatch.S) */
IMPORT void knl_dispatch_entry(void);		/* dispatch entry (int SVC_DISPATCH) */
IMPORT void knl_dispatch_to_schedtsk(void);	/* force dispatch (int SVC_FORCE_DISPATCH) */
IMPORT void knl_return_inthdr(void);		/* interrupt return path (vector.S) */
IMPORT void knl_syscall_entry(void);		/* SYSCALL (vector.S) */

IMPORT FP knl_intvec_tbl[N_INTVEC];		/* Interrupt vector table: TA_HLNG handlers */

/* Default handlers (exc_hdl.c) */
IMPORT void Default_Handler(UINT intno);	/* unregistered interrupt */

/* Boot information and timer (reset_main.c, sys_timer.c) */
IMPORT CONST T_BOOTINFO	*knl_bootinfo;		/* what the loader handed over (linear map) */
IMPORT UD	knl_kimg_pa;			/* where the kernel image is, physically */
IMPORT UD	knl_cntfrq;			/* time stamp counter frequency (Hz) */

/* Real time clock (rtc.c): UNIX seconds. E_NOSPT/E_IO/E_TMOUT when unavailable */
IMPORT ER	knl_rtc_read( UD *p_sec );
IMPORT ER	knl_rtc_write( UD sec );

/*
 * Memory as the loader's map says (reset_main.c): the RAM ranges for
 * the page allocator, and what in them stays the firmware's or the
 * loader's. The same names as the device tree's on the other
 * architecture, which the common parts read.
 */
#define KNL_MAX_MEMRANGE	64
#define KNL_MAX_MEMRSV		64
#define KNL_MAX_CPU		CNF_MAX_PRCID
#define KNL_BOOTARGS_LEN	BI_CMDLINE_LEN

typedef struct {
	UD	start;		/* physical start address */
	UD	size;		/* bytes */
} T_MEMRANGE;

IMPORT T_MEMRANGE knl_mem_range[KNL_MAX_MEMRANGE];
IMPORT INT	knl_mem_range_n;
IMPORT T_MEMRANGE knl_mem_rsv[KNL_MAX_MEMRSV];
IMPORT INT	knl_mem_rsv_n;
IMPORT UB	knl_bootargs[KNL_BOOTARGS_LEN];	/* the ESP's cmdline.txt */

/*
 * There is no device tree on a PC (ACPI says what it says instead). The
 * names the common parts read stay, empty: knl_dtb_addr is 0 and
 * knl_dtb_prop finds nothing. knl_dtb_fb is the screen of the loader,
 * whose pages the page allocator keeps out of use like the firmware's
 * screen of the other architecture.
 */
#define KNL_FB_FORMAT_LEN	16

typedef struct {
	UD	pa;		/* where the pixels are, physically */
	UD	size;		/* bytes */
	UW	width;
	UW	height;
	UW	stride;		/* bytes from one row to the next */
	UB	format[KNL_FB_FORMAT_LEN];	/* "x8r8g8b8", "x8b8g8r8" */
} T_DTBFB;

IMPORT UBINT	knl_dtb_addr;
IMPORT UD	knl_dtb_size;
IMPORT T_DTBFB	knl_dtb_fb;
IMPORT CONST UB	*knl_dtb_prop( CONST char *path, CONST char *prop, INT *p_len );
IMPORT UW	knl_dtb_u32( CONST char *path, CONST char *prop, UW dflt );

/* The screen console (x64/fbcon.c) */
IMPORT void	knl_fbcon_init( CONST T_BOOTINFO *bi );
IMPORT void	knl_fbcon_stop( void );
IMPORT void	knl_fbcon_resume( void );

/* MMU (mmu.c) */
IMPORT UD	knl_pt_l0_k_pa;			/* the kernel's PML4 (the loader's, its low half emptied) */
IMPORT void	knl_mmu_init( void );		/* the kernel's tables taken over */
IMPORT void	knl_mmu_cpu_init( void );	/* each processor: PAT, NX, global pages, PCID */
IMPORT void	*knl_dev_va( UBINT pa );	/* device window address of a physical register block */
IMPORT void	knl_dev_set_wc( UD pa, UD size );	/* that part of the window write-combining */
IMPORT BOOL	knl_dev_fault( UBINT va );	/* a fault in the window: its gigabyte mapped */
IMPORT ER	knl_pt_map( UD l0_pa, UBINT va, UD pa, UD size, UD attr );	/* 4KB page mappings */
IMPORT ER	knl_pt_unmap( UD l0_pa, UBINT va, UD size );
IMPORT UD	knl_pt_lookup( UD l0_pa, UBINT va );	/* PA of a page mapping, ~0 if none */
IMPORT UD	knl_pt_entry( UD l0_pa, UBINT va );	/* the page's entry, 0 if none */
IMPORT UBINT	knl_pt_next( UD l0_pa, UBINT va, UBINT end );	/* first mapped page in [va, end), or end */
IMPORT void	knl_tlb_flush_va( UBINT va );
IMPORT void	knl_pt_prune( UD l0_pa, UBINT va, UD size );	/* tables left empty under it taken out (user half) */
IMPORT BOOL	knl_pt_reclaim( void );			/* those taken out given back once safe; TRUE: some wait */
IMPORT UW	knl_pt_pruned( INT *p_waiting );	/* tables taken out so far (the tests) */
IMPORT void	knl_icache_sync( UBINT va, UD size );	/* written instructions made fetchable */
IMPORT void	knl_mmu_space_init_root( UD l0_pa );	/* a new PML4: the kernel's half copied in */
IMPORT void	knl_tlb_shootdown( UINT pcid, UBINT va, BOOL all );	/* the others drop it too */

/*
 * The instruction fetch is coherent with the stores of every processor
 * on x86-64: a program's pages written through the linear map are seen
 * as they are. Only the pipeline of a processor that may have fetched
 * the old bytes needs a serializing instruction, which every return to
 * a process makes (IRET, or the CR3 change before SYSRET).
 */
Inline void knl_icache_sync_all( void )
{
	Asm("" ::: "memory");
}

/*
 * The root of a process space as the MMU takes it (CR3): the physical
 * address of its PML4 and its PCID in bits 11:0. The kernel keeps that
 * value for each space and each task (ttbr0) and finds the space's
 * PCID again in it. A processor without PCIDs (CR4.PCIDE clear) reads
 * bits 11:0 as the cache bits of the PML4: the root then has the PCID
 * in bits 63:52 instead, which the dispatcher drops before it loads
 * CR3, and each change of CR3 empties the TLB of the old space.
 */
#define MMU_ASID_MAX	4095
#define MMU_ROOT_PA	U64(0x000FFFFFFFFFF000)

IMPORT BOOL	knl_pcid_on;			/* CR4.PCIDE set (mmu.c) */

Inline UD knl_mmu_space_root( UD l0_pa, UINT asid )
{
	return knl_pcid_on ? ( l0_pa | ( (UD)asid & MMU_ASID_MAX ) )
			   : ( l0_pa | ( (UD)asid << 52 ) );
}

Inline UINT knl_mmu_root_asid( UD root )
{
	return (UINT)( knl_pcid_on ? ( root & MMU_ASID_MAX ) : ( root >> 52 ) );
}

/* What is loaded into CR3 for a root (the dispatcher) */
Inline UD knl_mmu_root_cr3( UD root )
{
	return knl_pcid_on ? root : ( root & MMU_ROOT_PA );
}

Inline UINT knl_mmu_asid_top( void )
{
	return MMU_ASID_MAX;
}

/* Every processor's TLB entries of the ASID dropped (the space's tables are gone) */
Inline void knl_tlb_flush_asid( UINT asid )
{
	knl_tlb_shootdown(asid, 0, TRUE);
}

/* The machine of the programs the loader takes (ELF e_machine: EM_X86_64) */
#define KNL_ELF_MACHINE	62

/*
 * Task context block
 */
typedef struct {
	void	*ssp;		/* System stack pointer */
	UD	ttbr0;		/* process space (CR3 value with PCID); knl_ttbr0_null for kernel tasks */
	void	*usp;		/* initial user stack pointer (tasks of protection level 2/3) */
	void	*ustk;		/* user stack area base (NULL: none) */
	UD	ustk_npages;
	UD	fsbase;		/* TLS pointer (FS base) while not running */
} CTXB;

/*
 * Per-processor data (design 8.2). The GS base of each processor in the
 * kernel points at its own element, which starts with a pointer to
 * itself (knl_pcpu reads it with one load) and its prcid (spinlock.c
 * reads that). The fields replace the single-processor globals
 * knl_ctxtsk, knl_schedtsk, knl_dispatch_disabled, knl_taskindp,
 * knl_cur_ttbr0 and knl_fpu_ctx (macros in kernel.h / cpu_status.h).
 */
#define PC_OFFLINE	0
#define PC_BOOTING	1
#define PC_ONLINE	2
#define PC_STOPPED	3

typedef struct {
	UW	reserved0;
	UD	rsp0;		/* the system stack of the task when it entered from user mode */
	UD	rsp1, rsp2;
	UD	reserved1;
	UD	ist[7];
	UD	reserved2;
	UH	reserved3;
	UH	iopb;
} __attribute__((packed)) T_TSS;

typedef struct pcpu {
	struct pcpu *self;		/* this element (GS base + 0) */
	ID	prcid;			/* 1..CNF_MAX_PRCID (GS base + 8) */
	UINT	state;			/* PC_* */
	UD	mpidr;			/* the Local APIC ID */
	struct task_control_block *ctxtsk;	/* task in execution (NULL: idle) */
	struct task_control_block *schedtsk;	/* task to execute */
	INT	dispatch_disabled;	/* DDS_* */
	W	taskindp;		/* task independent part nesting */
	UD	cur_ttbr0;		/* CR3 installed */
	struct task_control_block *fpu_ctx;	/* task owning the x87/SSE/AVX state */
	void	*tmp_stack;		/* dispatcher/idle stack (top) */
	void	*irq_stack;		/* interrupt stack (top) */
	UD	ctxsw_cnt;		/* context switches */
	UD	ipi_cnt;		/* SGI_DISPATCH received */
	UD	user_rsp;		/* the user's rsp at SYSCALL, until it is in the frame */
	UD	kstack;			/* the running task's system stack top (= tss.rsp0) */
	volatile UD tlb_req;		/* SGI_TLB: what to drop (mmu.c) */
	volatile UW tlb_ack;
	UW	apic_id;
	T_TSS	tss;
	UB	ist_stack[3][IST_STACK_SIZE] __attribute__((aligned(16)));
} T_PCPU;

IMPORT T_PCPU	knl_pcpu_tbl[];
IMPORT INT	knl_num_prc;			/* processors online */

Inline T_PCPU *knl_pcpu( void )
{
	T_PCPU	*p;

	Asm("movq %%gs:0, %0" : "=r"(p));
	return p;
}

/* Multiprocessor support (smp.c) */
IMPORT UINT	knl_lock_kernel( void );		/* big kernel lock: DI + lock, re-entrant per processor */
IMPORT void	knl_unlock_kernel( UINT intsts );	/* unlock + restore interrupts */
IMPORT void	knl_unlock_kernel_keep( void );		/* unlock, interrupts stay as they are */
IMPORT BOOL	knl_kernel_locked( void );
IMPORT void	knl_reschedule( void );			/* assign ready tasks to processors */
IMPORT struct task_control_block *knl_dispatch_pick( void );	/* dispatcher: take schedtsk under the lock */
IMPORT void	knl_acct_leave( void );			/* dispatcher: the task that ran charged its time */
IMPORT void	knl_send_ipi( UW prcmask, UINT sgino );
IMPORT void	knl_sync_cores( UD ttbr0 );		/* the others running in a space take an interrupt */
IMPORT void	knl_smp_init_primary( void );
IMPORT void	knl_smp_init_interrupt( void );
IMPORT void	knl_smp_start_secondaries( void );
IMPORT void	knl_cpu_init( T_PCPU *pc );		/* this processor's GDT, TSS, IDT, GS base (smp.c) */

/* Local APIC and IOAPIC (apic.c) */
IMPORT void	knl_apic_init( void );			/* the boot processor's, and the IOAPICs */
IMPORT void	knl_apic_cpu_init( void );		/* this processor's Local APIC */
IMPORT UW	knl_apic_id( void );
IMPORT void	knl_apic_send( UW apic_id, UW vec, UW mode );	/* an interrupt to another processor */
IMPORT void	knl_apic_eoi( void );
IMPORT void	knl_apic_timer_deadline( UD tsc );	/* the system timer's expiry, 0: none */
IMPORT void	knl_apic_timer_isr( UINT intno, UW iar );
IMPORT BOOL	knl_x2apic_on;
#define APIC_DM_FIXED	0x000
#define APIC_DM_NMI	0x400
#define APIC_DM_INIT	0x500
#define APIC_DM_SIPI	0x600

/* ACPI tables (kernel/sysdepend/acpi) */
#include "../../../acpi/acpi.h"

/* Finding the machine stopped (lockup.c, design 14.10) */
IMPORT void	knl_lockup_cpu_init( void );
IMPORT void	knl_lockup_init_interrupt( void );
IMPORT void	knl_lockup_arm( void );			/* the checks begin */
IMPORT BOOL	knl_wdog_hw_feed( UW ms );		/* the board's watchdog: reset ms from now; FALSE: none */
IMPORT void	knl_wdog_hw_stop( void );
IMPORT void	knl_lockup_say_task( CONST char *pre, struct task_control_block *t );	/* urgent output */
IMPORT void	knl_lockup_say_frames( CONST char *pre, UBINT fp );

/* TCB system dependent initialization/finalization (called by tk_cre_tsk / task deletion) */
#define DEFINE_TSK_SYSDEPEND
IMPORT ER	knl_tcb_sysdep_cre( TCB *tcb, CONST T_CTSK *pk_ctsk );
IMPORT ER	knl_tcb_sysdep_del( TCB *tcb );

/* User stack size and process space of the next user task (cpu_cntl.c) */
IMPORT UD	knl_ustack_size;
IMPORT void	knl_set_ustack_size( UD size );
IMPORT ER	knl_tsk_set_space( ID tskid, UD ttbr0 );

/* The x87/SSE/AVX state (cpu_cntl.c): XSAVE when there is, else FXSAVE */
IMPORT UW	knl_xsave_size;			/* bytes of a task's area, 64 aligned */
IMPORT BOOL	knl_xsave_on;
IMPORT UD	knl_xcr0;
IMPORT void	knl_fpu_cpu_init( void );

/* Supervisor mode access prevention: the way to the user pages (cpu_uaccess.h) */
#include "cpu_uaccess.h"

#endif /* _SYSDEPEND_CPU_CORE_SYSDEPEND_ */
