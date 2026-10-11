/*
 *----------------------------------------------------------------------
 *    TessronOS
 *    Copyright (C) 2026 satromi
 *
 *    This software is distributed under the MIT License (LICENSE).
 *----------------------------------------------------------------------
 */

/*
 *	vhc.h
 *	The USB controller in software that the tests plug devices into
 *	(vhc.c, USE_KTEST).
 */

#ifndef __VHC_H__
#define __VHC_H__

#define VHC_MAX_IF	4		/* interfaces with a report descriptor */
#define VHC_MAX_STR	4		/* strings, index 1 to 4 */

/*
 * A device: its descriptors, and what it does with what is sent to it.
 * out: bytes sent to an OUT endpoint (a bulk or interrupt transfer).
 * klass: a class request to an interface; how many bytes it answered
 * with in data (up to len), or a negative number to stall. NULL: the
 * requests that send nothing are taken and the others stall.
 */
typedef struct {
	CONST UB	*ddesc;			/* 18 bytes */
	CONST UB	*cfg;			/* the whole configuration */
	INT		cfglen;
	CONST char	*str[VHC_MAX_STR];	/* ASCII, string descriptors 1 to 4 */
	CONST UB	*report[VHC_MAX_IF];	/* HID report descriptors, by interface */
	INT		report_len[VHC_MAX_IF];
	void		(*out)( UB ep, CONST UB *data, INT len );
	INT		(*klass)( CONST UB *setup, UB *data, INT len );
} T_VHCDEV;

/* The device in the controller's port (0 or 1), the controller added the first time */
IMPORT ER knl_vhc_plug( INT port, CONST T_VHCDEV *dev );
IMPORT ER knl_vhc_unplug( INT port );

/* A piece of up to 64 bytes for the IN endpoint ep, given to its next transfer */
IMPORT ER knl_vhc_in( INT port, UB ep, CONST UB *data, INT len );

/* usbman.c: a controller added after the machine started (with the manager's lock held) */
IMPORT ER   knl_usb_add_hc( USBHC *hc );

#endif /* __VHC_H__ */
