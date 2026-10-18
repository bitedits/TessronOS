/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	vio_pci.h
 *	The virtio transport over PCI (virtio 1.x, "modern"), in the
 *	register model of virtio-mmio: the drivers written for virtio-mmio
 *	(device/vnet/vnet.c) read and write the same register offsets, and
 *	vio_pci.c turns each into its place in the PCI function's structures
 *	(the common configuration, the notification area, the ISR status,
 *	the device configuration). The interrupt is one MSI-X message for
 *	the configuration and every queue.
 */

#ifndef _DEVICE_VIRTIO_VIO_PCI_H_
#define _DEVICE_VIRTIO_VIO_PCI_H_

#include <ts/pcie.h>

/* The virtio-mmio register offsets the drivers use (virtio 1.x, version 2) */
#define VIO_MMIO_MAGIC			0x000
#define VIO_MMIO_VERSION		0x004
#define VIO_MMIO_DEVICE_ID		0x008
#define VIO_MMIO_DEVICE_FEATURES	0x010
#define VIO_MMIO_DEVICE_FEATURES_SEL	0x014
#define VIO_MMIO_DRIVER_FEATURES	0x020
#define VIO_MMIO_DRIVER_FEATURES_SEL	0x024
#define VIO_MMIO_QUEUE_SEL		0x030
#define VIO_MMIO_QUEUE_NUM_MAX		0x034
#define VIO_MMIO_QUEUE_NUM		0x038
#define VIO_MMIO_QUEUE_READY		0x044
#define VIO_MMIO_QUEUE_NOTIFY		0x050
#define VIO_MMIO_INTERRUPT_STATUS	0x060
#define VIO_MMIO_INTERRUPT_ACK		0x064
#define VIO_MMIO_STATUS			0x070
#define VIO_MMIO_QUEUE_DESC_LOW		0x080
#define VIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIO_MMIO_QUEUE_DRIVER_LOW	0x090
#define VIO_MMIO_QUEUE_DRIVER_HIGH	0x094
#define VIO_MMIO_QUEUE_DEVICE_LOW	0x0a0
#define VIO_MMIO_QUEUE_DEVICE_HIGH	0x0a4
#define VIO_MMIO_CONFIG			0x100

#define VIO_PCI_MAX_QUEUE		8

typedef struct {
	T_PCIDEV	pci;
	UBINT		common;		/* the common configuration */
	UBINT		notify;		/* the notification area */
	UW		notify_mul;	/* its offset multiplier */
	UBINT		isr;		/* the ISR status */
	UBINT		device;		/* the device's configuration */
	UINT		intno;		/* the MSI-X message, 0 none */
	UH		qsel;		/* the queue selected */
	UH		notify_off[VIO_PCI_MAX_QUEUE];
} T_VIOPCI;

/*
 * The index'th virtio function of device type 'id' (1 network, 2 block
 * ...): its structures found, its windows and bus mastering on, its
 * MSI-X message given. E_NOEXS when there is none.
 */
IMPORT ER	vio_pci_find( UINT id, INT index, T_VIOPCI *v );

/* A virtio-mmio register, as the PCI function has it */
IMPORT void	vio_pci_out( T_VIOPCI *v, UW off, UW val );
IMPORT UW	vio_pci_in( T_VIOPCI *v, UW off );

#endif /* _DEVICE_VIRTIO_VIO_PCI_H_ */
