/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	vio_pci.c
 *	The virtio transport over PCI (vio_pci.h).
 *
 *	A modern virtio function (vendor 1AF4h, device 1040h + type, or the
 *	transitional 1000h + type - 1 with the modern structures too) names
 *	its structures in vendor capabilities of its configuration space:
 *	the type, the window, the offset in it. The common configuration
 *	holds what the registers of virtio-mmio hold, a queue's at a time
 *	(queue_select); a queue is notified by a write of its number at its
 *	own place of the notification area. The 64 bit addresses of a queue
 *	are written as two 32 bit halves, as the specification lets a driver.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "vio_pci.h"

#if defined(PCIE_ECAM_PA) || defined(PCIE_CFG_BOARD)

#define VIO_VENDOR		0x1af4
#define VIO_CAP_VENDOR		0x09

#define VIO_CAP_COMMON		1
#define VIO_CAP_NOTIFY		2
#define VIO_CAP_ISR		3
#define VIO_CAP_DEVICE		4

/* The common configuration */
#define CC_DFSELECT		0x00
#define CC_DF			0x04
#define CC_GFSELECT		0x08
#define CC_GF			0x0c
#define CC_MSIX			0x10
#define CC_NUMQ			0x12
#define CC_STATUS		0x14
#define CC_CFGGEN		0x15
#define CC_Q_SELECT		0x16
#define CC_Q_SIZE		0x18
#define CC_Q_MSIX		0x1a
#define CC_Q_ENABLE		0x1c
#define CC_Q_NOFF		0x1e
#define CC_Q_DESCLO		0x20
#define CC_Q_DESCHI		0x24
#define CC_Q_AVAILLO		0x28
#define CC_Q_AVAILHI		0x2c
#define CC_Q_USEDLO		0x30
#define CC_Q_USEDHI		0x34

#define VIO_MSI_NONE		0xffff

Inline void wr32( UBINT a, UW v ) { *(volatile UW *)a = v; }
Inline void wr16( UBINT a, UH v ) { *(volatile UH *)a = v; }
Inline void wr8( UBINT a, UB v ) { *(volatile UB *)a = v; }
Inline UW rd32( UBINT a ) { return *(volatile UW *)a; }
Inline UH rd16( UBINT a ) { return *(volatile UH *)a; }
Inline UB rd8( UBINT a ) { return *(volatile UB *)a; }

/* Where the capability at 'cap' points */
LOCAL UBINT cap_where( T_VIOPCI *v, UINT cap )
{
	T_PCIDEV	*d = &v->pci;
	UINT		bar = ts_pcie_cfg_read(d->bus, d->dev, d->fn, cap + 4, 1);
	UW		off = ts_pcie_cfg_read(d->bus, d->dev, d->fn, cap + 8, 4);
	UBINT		base;

	if ( bar >= PCI_MAX_BAR ) {
		return 0;
	}
	base = ts_pcie_bar_base(d, (INT)bar);
	return ( base != 0 ) ? base + off : 0;
}

EXPORT ER vio_pci_find( UINT id, INT index, T_VIOPCI *v )
{
	INT	i, n = ts_pcie_count(), k = 0;

	for ( i = 0; i < n; i++ ) {
		T_PCIDEV	d;
		UINT		off, guard;
		UW		sts;

		if ( ts_pcie_get(i, &d) < E_OK || d.vendor != VIO_VENDOR ) continue;
		if ( d.device != 0x1040 + id && d.device != 0x1000 + id - 1 ) continue;
		if ( k++ != index ) continue;

		knl_memset(v, 0, sizeof(*v));
		v->pci = d;
		ts_pcie_enable(&d);
		sts = ts_pcie_cfg_read(d.bus, d.dev, d.fn, PCI_STATUS, 2);
		if ( ( sts & 0x0010 ) == 0 ) {
			return E_NOSPT;
		}
		off = ts_pcie_cfg_read(d.bus, d.dev, d.fn, PCI_CAP_PTR, 1) & 0xfc;
		for ( guard = 0; guard < 48 && off >= 0x40; guard++ ) {
			UW	hdr = ts_pcie_cfg_read(d.bus, d.dev, d.fn, off, 4);

			if ( ( hdr & 0xff ) == VIO_CAP_VENDOR ) {
				switch ( ( hdr >> 24 ) & 0xff ) {
				case VIO_CAP_COMMON:
					if ( v->common == 0 ) v->common = cap_where(v, off);
					break;
				case VIO_CAP_NOTIFY:
					if ( v->notify == 0 ) {
						v->notify = cap_where(v, off);
						v->notify_mul = ts_pcie_cfg_read(d.bus, d.dev, d.fn, off + 16, 4);
					}
					break;
				case VIO_CAP_ISR:
					if ( v->isr == 0 ) v->isr = cap_where(v, off);
					break;
				case VIO_CAP_DEVICE:
					if ( v->device == 0 ) v->device = cap_where(v, off);
					break;
				}
			}
			off = ( hdr >> 8 ) & 0xfc;
		}
		if ( v->common == 0 || v->notify == 0 ) {
			return E_NOSPT;			/* only the legacy interface */
		}
		if ( ts_pcie_msi_alloc(&d, &v->intno) < E_OK ) {
			v->intno = 0;
		}
		return E_OK;
	}
	return E_NOEXS;
}

EXPORT void vio_pci_out( T_VIOPCI *v, UW off, UW val )
{
	UBINT	c = v->common;

	if ( off >= VIO_MMIO_CONFIG ) {
		if ( v->device != 0 ) wr32(v->device + off - VIO_MMIO_CONFIG, val);
		return;
	}
	switch ( off ) {
	case VIO_MMIO_DEVICE_FEATURES_SEL:	wr32(c + CC_DFSELECT, val); break;
	case VIO_MMIO_DRIVER_FEATURES_SEL:	wr32(c + CC_GFSELECT, val); break;
	case VIO_MMIO_DRIVER_FEATURES:		wr32(c + CC_GF, val); break;
	case VIO_MMIO_QUEUE_SEL:
		v->qsel = (UH)val;
		wr16(c + CC_Q_SELECT, (UH)val);
		break;
	case VIO_MMIO_QUEUE_NUM:		wr16(c + CC_Q_SIZE, (UH)val); break;
	case VIO_MMIO_QUEUE_READY:
		if ( val != 0 ) {
			/* every queue raises the one message, as does the configuration */
			wr16(c + CC_Q_MSIX, ( v->intno != 0 ) ? 0 : VIO_MSI_NONE);
			wr16(c + CC_MSIX, ( v->intno != 0 ) ? 0 : VIO_MSI_NONE);
			if ( v->qsel < VIO_PCI_MAX_QUEUE ) {
				v->notify_off[v->qsel] = rd16(c + CC_Q_NOFF);
			}
		}
		wr16(c + CC_Q_ENABLE, (UH)val);
		break;
	case VIO_MMIO_QUEUE_NOTIFY:
		DeviceBarrier();
		wr16(v->notify + (UBINT)( ( val < VIO_PCI_MAX_QUEUE ) ? v->notify_off[val] : 0 ) * v->notify_mul,
		     (UH)val);
		break;
	case VIO_MMIO_INTERRUPT_ACK:		break;	/* the ISR status clears as it is read */
	case VIO_MMIO_STATUS:			wr8(c + CC_STATUS, (UB)val); break;
	case VIO_MMIO_QUEUE_DESC_LOW:		wr32(c + CC_Q_DESCLO, val); break;
	case VIO_MMIO_QUEUE_DESC_HIGH:		wr32(c + CC_Q_DESCHI, val); break;
	case VIO_MMIO_QUEUE_DRIVER_LOW:		wr32(c + CC_Q_AVAILLO, val); break;
	case VIO_MMIO_QUEUE_DRIVER_HIGH:	wr32(c + CC_Q_AVAILHI, val); break;
	case VIO_MMIO_QUEUE_DEVICE_LOW:		wr32(c + CC_Q_USEDLO, val); break;
	case VIO_MMIO_QUEUE_DEVICE_HIGH:	wr32(c + CC_Q_USEDHI, val); break;
	default:				break;
	}
	DeviceBarrier();
}

EXPORT UW vio_pci_in( T_VIOPCI *v, UW off )
{
	UBINT	c = v->common;

	if ( off >= VIO_MMIO_CONFIG ) {
		return ( v->device != 0 ) ? rd32(v->device + off - VIO_MMIO_CONFIG) : 0;
	}
	switch ( off ) {
	case VIO_MMIO_MAGIC:			return 0x74726976;
	case VIO_MMIO_VERSION:			return 2;
	case VIO_MMIO_DEVICE_FEATURES:		return rd32(c + CC_DF);
	case VIO_MMIO_QUEUE_NUM_MAX:		return rd16(c + CC_Q_SIZE);
	case VIO_MMIO_QUEUE_READY:		return rd16(c + CC_Q_ENABLE);
	case VIO_MMIO_INTERRUPT_STATUS:		return ( v->isr != 0 ) ? rd8(v->isr) : 0;
	case VIO_MMIO_STATUS:			return rd8(c + CC_STATUS);
	default:				return 0;
	}
}

#endif /* PCIE_ECAM_PA || PCIE_CFG_BOARD */
