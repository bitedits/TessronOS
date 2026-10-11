/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	uiprn.h
 *	印刷 from a process (design 11.22.6)
 *
 *	A process that prints makes its pages as records of xmlTAD -- a
 *	document, which may run to several pages of its paper, or a figure,
 *	one sheet of its paper -- in an object made for them, and asks the
 *	desktop with DT_RQ_PRINT (ts/dtreq.h) to print them. The desktop
 *	asks how with its panels, shows them when asked, writes the PDF file
 *	and puts it on the desktop. The object is thrown away afterwards.
 *
 *	These calls make and fill the object; the request itself goes the
 *	way each program asks the desktop anything.
 */

#ifndef __TS_UIPRN_H__
#define __TS_UIPRN_H__

#include <ts/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	TS_UUID	obj;			/* the object the pages are in */
	ID	key;
	INT	pages;			/* records written */
	ER	er;			/* the first thing that went wrong, E_OK none */
} T_UIPRN;

/* The object made, near 'near' (NULL: anywhere) */
IMPORT ER   ui_prn_open( T_UIPRN *p, CONST TS_UUID *near );
/* One page of xmlTAD added as a record of its own */
IMPORT ER   ui_prn_page( T_UIPRN *p, CONST UB *xml, INT len );
/* Done writing: the object closed, ready to be named in DT_RQ_PRINT */
IMPORT ER   ui_prn_ready( T_UIPRN *p );
/* The object thrown away */
IMPORT void ui_prn_close( T_UIPRN *p );

#ifdef __cplusplus
}
#endif

#endif /* __TS_UIPRN_H__ */
