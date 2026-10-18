/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpu_msi.h
 *	Where a PCI Express function sends its message interrupts (ARMv8-A
 *	core with a GICv2m frame). peripheral_kernel/sysman/pcie.c writes
 *	the address and the data given here into an entry of the
 *	function's table; how the entry is found and written is the same
 *	on every machine.
 *
 *	KNL_HAS_MSI is defined when the machine has somewhere to send them
 *	(the board's sysdef.h names GICV2M_BASE).
 */

#ifndef _SYSDEPEND_CPU_CORE_MSI_
#define _SYSDEPEND_CPU_CORE_MSI_

#ifdef GICV2M_BASE
#define KNL_HAS_MSI	1

/*
 * The frame that turns a message into an interrupt. A card writes a
 * number to one register of it and the distributor raises that number,
 * so a card needs no wire of its own and no line shared with others.
 *
 * Which numbers may be used the frame says itself, so nothing here is
 * built into the board's list of addresses beyond where the frame is.
 */
#define V2M_MSI_TYPER		0x008
#define V2M_MSI_SETSPI_NS	0x040

#define V2M_TYPER_BASE(v)	(((v) >> 16) & 0x3ff)
#define V2M_TYPER_NUM(v)	((v) & 0x3ff)

/* The interrupt numbers messages may raise: num of them from first */
Inline void knl_msi_span( UINT *p_first, UINT *p_num )
{
	UW	typer = *(volatile UW *)(GICV2M_BASE + V2M_MSI_TYPER);

	*p_first = V2M_TYPER_BASE(typer);
	*p_num   = V2M_TYPER_NUM(typer);
}

/*
 * Ready the interrupt intno for messages. A message is an edge, not a
 * level. The frame raises the number for an instant and lets it fall
 * at once; a distributor that is told the number is a level only sees
 * it while it is high, and by the time it looks it has fallen. Every
 * message would be lost, and whatever waits on them would wait out its
 * whole time each time instead.
 */
Inline void knl_msi_prepare( UINT intno )
{
	SetIntMode(intno, IM_EDGE);
}

/* The address a message for intno is written to, and what is written */
Inline UD knl_msi_addr( UINT intno )
{
	(void)intno;
	return (UD)(GICV2M_PA + V2M_MSI_SETSPI_NS);
}

Inline UW knl_msi_data( UINT intno )
{
	return (UW)intno;
}

#endif /* GICV2M_BASE */

#endif /* _SYSDEPEND_CPU_CORE_MSI_ */
