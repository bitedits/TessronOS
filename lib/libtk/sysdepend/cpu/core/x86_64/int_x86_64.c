/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	int_x86_64.c
 *	Interrupt control library (x86-64, Local APIC and IOAPIC)
 *
 *	An interrupt number is a vector (sysdef.h). The inputs of the
 *	IOAPIC are masked and unmasked there; a message signalled interrupt
 *	is masked at its device, so enabling one here is only taking note;
 *	the timer and the inter-processor interrupts are the Local APIC's
 *	own. A level of priority is the vector's class and cannot be set
 *	apart from it.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_X86_64

#include <tk/tkernel.h>

IMPORT void	knl_apic_eoi( void );
IMPORT void	knl_apic_set_tpr( UW tpr );
IMPORT UW	knl_apic_get_tpr( void );
IMPORT BOOL	knl_apic_pending( UINT vec );
IMPORT void	knl_ioapic_mask( UINT gsi, BOOL mask );
IMPORT void	knl_ioapic_mode( UINT gsi, UINT mode );

#define IS_GSI(v)	( (v) >= INTVEC_GSI(0) && (v) < INTVEC_GSI(INTVEC_GSI_NUM) )

/*
 * Disable interrupts, return 1 when they were disabled already
 */
EXPORT UW disint( void )
{
	UD	fl;

	Asm("pushfq; popq %0; cli" : "=r"(fl) :: "memory");
	return ( fl & RFL_IF ) ? 0 : 1;
}

/*
 * Restore what disint() returned
 */
EXPORT void enaint( UW intsts )
{
	if ( intsts != 0 ) {
		Asm("cli" ::: "memory");
	} else {
		Asm("sti" ::: "memory");
	}
}

/*
 * Interrupt controller mask level (the Local APIC's task priority):
 * interrupts of level >= 'level' are masked, that is the classes
 * 15 - level and below
 */
EXPORT void SetCtrlIntLevel( INT level )
{
	INT	cls;

	if ( level < INTPRI_HIGHEST || level > INTPRI_LOWEST + 1 ) return;
	cls = 15 - level;
	knl_apic_set_tpr( ( cls > 0 ) ? (UW)cls << 4 : 0 );
}

EXPORT INT GetCtrlIntLevel( void )
{
	UW	tpr = knl_apic_get_tpr() >> 4;

	return ( tpr == 0 ) ? INTPRI_LOWEST + 1 : 15 - (INT)tpr;
}

EXPORT void EnableInt( UINT intno, INT level )
{
	(void)level;
	if ( intno >= N_INTVEC ) return;
	if ( IS_GSI(intno) ) {
		knl_ioapic_mask(intno - INTVEC_GSI(0), FALSE);
	}
}

EXPORT void DisableInt( UINT intno )
{
	if ( intno >= N_INTVEC ) return;
	if ( IS_GSI(intno) ) {
		knl_ioapic_mask(intno - INTVEC_GSI(0), TRUE);
	}
}

EXPORT void SetIntMode( UINT intno, UINT mode )
{
	if ( intno >= N_INTVEC ) return;
	if ( IS_GSI(intno) ) {
		knl_ioapic_mode(intno - INTVEC_GSI(0), mode);
	}
}

EXPORT void ClearInt( UINT intno )
{
	(void)intno;		/* a vector the APIC has accepted is ended, not cleared */
}

EXPORT BOOL CheckInt( UINT intno )
{
	if ( intno >= N_INTVEC ) return FALSE;
	return knl_apic_pending(intno);
}

EXPORT void EndOfInt( UINT intno )
{
	(void)intno;
	knl_apic_eoi();
}

#endif /* CPU_CORE_X86_64 */
