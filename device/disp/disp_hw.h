/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	disp_hw.h
 *	What the part of the screen driver that knows the machine hands
 *	over to the part that does not (design 16.5).
 */

#ifndef __TS_DISP_HW_H__
#define __TS_DISP_HW_H__

typedef struct {
	UINT	width;
	UINT	height;
	UINT	pitch;			/* bytes from one row to the next */
	UINT	bpp;
	UINT	format;			/* DISP_FMT_* */
	UD	fb_pa;			/* where the pixels are, physically */
	UBINT	fb_va;			/* and how this kernel reaches them */
	UINT	alpha;			/* DISP_ALPHA_*: what the top byte must hold */
	CONST char *via;		/* how it was found, for the console */

	/*
	 * Two screens' worth of rows, one under the other, of which the
	 * screen shows either: what is sent goes to the one not shown,
	 * which is then shown (disp.c). Left 0 and NULL by a screen that
	 * has one.
	 *
	 *   pages	screens' worth of rows mapped from fb_va: 1 or 2
	 *   flip	the screen to show page 0 or 1 from its next refresh
	 *   vsync	wait for the next vertical blank; NULL when there is
	 *		no way to
	 *   latch_us	how long after a flip it has surely been taken
	 *		without waiting for a blank; 0 at once
	 *   flip_start	whether to flip from the start (else only when asked,
	 *		ts_disp_flip)
	 */
	UINT	pages;
	ER	(*flip)( UINT page );
	ER	(*vsync)( void );
	UINT	latch_us;
	BOOL	flip_start;
	UBINT	page2_va;		/* the second page, when it is not the rows after the first; 0 */

	/*
	 * The refresh as a clock, where the screen can say when its blanks
	 * end (left 0 and NULL where it cannot). With it a send that comes
	 * after the flip before has surely been taken does not wait at all,
	 * and one that comes long before the blank sleeps until just before
	 * it.
	 *
	 *   frame_ns	the time from the end of one blank to the end of the next
	 *   blank_ns	how long a blank lasts
	 *   blank_seen	when the end of a blank was last seen, in ns of
	 *		ts_get_mono; 0 when none has been
	 */
	UD	frame_ns;
	UD	blank_ns;
	UD	(*blank_seen)( void );
} T_DISPHW;

/*
 * A refresh made up for a screen that has none to wait for (QEMU's), so
 * that the waits for the blank and the pace they set can be tried
 * there: a blank of a twentieth of the period ends every period_us from
 * the call, and a flip is taken at the end of the first blank that
 * begins after it, as on a screen that has one. Flipping must be on.
 * 0 puts the screen back as it was.
 */
IMPORT ER knl_disp_sim_blank( UINT period_us );

/*
 * The byte above the colour in a four byte pixel. Most screens do not
 * look at it; one that takes it for opacity has to be sent it the way
 * it counts, whatever the drawing left there.
 */
#define DISP_ALPHA_ANY		0	/* not looked at: sent as drawn */
#define DISP_ALPHA_ZERO		1	/* 0 is opaque */
#define DISP_ALPHA_FF		2	/* 0xFF is opaque */

/*
 * Find the screen, set a mode, and map its memory. Answers 1 when one
 * was found, 0 when the machine has none, or an error.
 *
 * The mapping has to let writes merge and reorder, which is what
 * ordinary non-cacheable memory does. The device window will not do: it
 * forbids both, so every word written goes to the bus on its own.
 */
IMPORT INT knl_disp_hw_init( T_DISPHW *hw );

/*
 * The screen set to another size, w x h, as the machine's settings ask
 * (システム環境設定). 'hw' is what init gave and is changed to what the
 * new size is; its pixels are mapped anew, two pages of them when it
 * had two and the new size leaves room for them. Answers 1 when it took, 0
 * when this screen cannot be set to that size (it keeps the one it had),
 * or an error.
 */
IMPORT INT knl_disp_hw_setmode( T_DISPHW *hw, UINT w, UINT h );

#endif /* __TS_DISP_HW_H__ */
