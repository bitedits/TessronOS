/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpu_msi.h (x86-64)
 *	Message signalled interrupts: what a PCI Express function writes to
 *	raise one. The PCI side of it (the MSI and MSI-X capabilities,
 *	sysman/pcie.c) is the same on every machine; this says only the
 *	address and the data.
 *
 *	A message is a write to the Local APIC's window at 0xFEE00000 with
 *	the destination's APIC ID in bits 19:12; its data is the vector,
 *	delivered as a fixed interrupt on an edge. The vectors of
 *	INTVEC_MSI_FIRST on are given to messages (sysdef.h), and all of
 *	them go to the first processor, which takes the devices'
 *	interrupts as on the other architecture.
 */

#ifndef _SYSDEPEND_CPU_CORE_MSI_
#define _SYSDEPEND_CPU_CORE_MSI_

#define KNL_HAS_MSI	1

#define MSI_ADDR_BASE	0xfee00000u

IMPORT UW	knl_msi_dest;		/* the APIC ID messages go to (apic.c) */

/* The interrupt numbers messages may raise: num of them from first */
Inline void knl_msi_span( UINT *p_first, UINT *p_num )
{
	*p_first = INTVEC_MSI_FIRST;
	*p_num   = INTVEC_MSI_NUM;
}

/* Nothing to set up: a message is always taken as an edge */
Inline void knl_msi_prepare( UINT intno )
{
	(void)intno;
}

/* The address a message for intno is written to, and what is written */
Inline UD knl_msi_addr( UINT intno )
{
	(void)intno;
	return (UD)( MSI_ADDR_BASE | ( ( knl_msi_dest & 0xff ) << 12 ) );
}

Inline UW knl_msi_data( UINT intno )
{
	return (UW)( intno & 0xff );
}

#endif /* _SYSDEPEND_CPU_CORE_MSI_ */
