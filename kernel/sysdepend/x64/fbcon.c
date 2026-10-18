/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	fbcon.c (x64 PC)
 *	The screen console: what goes to the console, drawn on the screen
 *	the UEFI loader set up (Graphics Output Protocol), in white on
 *	black with an 8 x 16 bitmap font (confont.h). It works from the
 *	first line the kernel says, before there is any other way to draw,
 *	and is the console of a machine without a serial port (the Surface
 *	Go 2, docs/private/x64_surface_go2_study.md 2.13).
 *
 *	It never scrolls: reading the screen back is slow, so when the last
 *	row is written the next line goes to the top again, and the row
 *	after the one being written is kept clear so that the newest line
 *	can be found. The screen's driver takes the screen over when it
 *	starts (knl_fbcon_stop); what stops the kernel brings the console
 *	back (knl_fbcon_resume).
 */

#include <sys/machine.h>

#ifdef X64

#include <tk/tkernel.h>
#include <sys/sysdef.h>
#include "../cpu/core/x86_64/bootinfo.h"
#include "confont.h"

IMPORT void	(*tm_mirror)( const UB *buf, INT size );
IMPORT void	knl_dev_set_wc( UD pa, UD size );	/* mmu.c */

LOCAL _UW	*fb = NULL;		/* the pixels (device window) */
LOCAL UW	fb_w, fb_h, fb_pitch;	/* pixels; pixels from one row to the next */
LOCAL UW	cols, rows, cx, cy;
LOCAL UW	fg = 0x00e0e0e0, bg = 0x00000000;
LOCAL BOOL	stopped = FALSE;

LOCAL void clear_row( UW r )
{
	UW	y, x;

	for ( y = r * CONFONT_H; y < ( r + 1 ) * CONFONT_H && y < fb_h; y++ ) {
		_UW	*p = fb + (UD)y * fb_pitch;

		for ( x = 0; x < cols * CONFONT_W; x++ ) {
			p[x] = bg;
		}
	}
}

LOCAL void newline( void )
{
	cx = 0;
	if ( ++cy >= rows ) {
		cy = 0;
	}
	clear_row(cy);
	clear_row(( cy + 1 ) % rows);
}

LOCAL void draw( UB c )
{
	CONST UB	*g;
	UW		y, x;

	if ( c < CONFONT_FIRST || c > CONFONT_LAST ) {
		c = '?';
	}
	g = confont[c - CONFONT_FIRST];
	for ( y = 0; y < CONFONT_H; y++ ) {
		_UW	*p = fb + (UD)( cy * CONFONT_H + y ) * fb_pitch + cx * CONFONT_W;
		UB	bits = g[y];

		for ( x = 0; x < CONFONT_W; x++ ) {
			p[x] = ( bits & ( 0x80 >> x ) ) ? fg : bg;
		}
	}
	if ( ++cx >= cols ) {
		newline();
	}
}

LOCAL void fbcon_out( const UB *buf, INT size )
{
	if ( stopped ) {
		return;
	}
	while ( size-- > 0 ) {
		UB	c = *buf++;

		if ( c == '\n' ) {
			newline();
		} else if ( c == '\r' ) {
			cx = 0;
		} else if ( c == '\t' ) {
			do {
				draw(' ');
			} while ( ( cx & 7 ) != 0 && cx != 0 );
		} else if ( c == '\b' ) {
			if ( cx > 0 ) cx--;
		} else {
			draw(c);
		}
	}
}

/*
 * Set up on the screen of the T_BOOTINFO; a screen of another kind than
 * 32 bits a pixel is left alone. The rows of the screen, which is a
 * device's memory, are made write-combining in the device window first.
 */
EXPORT void knl_fbcon_init( CONST T_BOOTINFO *bi )
{
	UW	r;

	if ( bi->fb.pa == 0 || bi->fb.format == BI_FB_NONE || bi->fb.width < CONFONT_W * 20 ) {
		return;
	}
	knl_dev_set_wc(bi->fb.pa, bi->fb.size);
	fb = (_UW *)DEV_BASE(bi->fb.pa);
	fb_w = bi->fb.width;
	fb_h = bi->fb.height;
	fb_pitch = bi->fb.stride / 4;
	cols = fb_w / CONFONT_W;
	rows = fb_h / CONFONT_H;
	cx = cy = 0;
	if ( bi->fb.format == BI_FB_MASK ) {
		fg = ( bi->fb.mask_r | bi->fb.mask_g | bi->fb.mask_b ) & 0xe0e0e0e0;
	}
	for ( r = 0; r < rows; r++ ) {
		clear_row(r);
	}
	tm_mirror = fbcon_out;
}

/* The screen's driver has the screen now */
EXPORT void knl_fbcon_stop( void )
{
	stopped = TRUE;
}

/* What stops the machine is said on the screen again, over whatever is there */
EXPORT void knl_fbcon_resume( void )
{
	if ( fb != NULL && stopped ) {
		stopped = FALSE;
		cx = 0;
		clear_row(cy);
		clear_row(( cy + 1 ) % rows);
	}
}

#endif /* X64 */
