/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	disp_gop.c
 *	The screen of a PC: the framebuffer the UEFI firmware set up
 *	(Graphics Output Protocol) before the loader left its boot services
 *	(design 16.5.1). Its size and its pixels' order are what the loader
 *	handed over (bootinfo.h); nothing sets a mode any more after that,
 *	so the size cannot be changed while the system runs. The pixels are
 *	mapped write-combining (VMAP_NOCACHE), the screen console stops
 *	drawing on them, and the layers above work as on the other machines.
 */

#include <sys/machine.h>

#ifdef X64

#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <ts/disp.h>
#include "sysman/pfalloc.h"
#include "sysdepend/sysdepend.h"
#include "disp_hw.h"

EXPORT INT knl_disp_hw_init( T_DISPHW *hw )
{
	CONST T_BIFB	*fb;
	UD		base, npages, size;
	UBINT		va;

	if ( hw == NULL ) {
		return E_PAR;
	}
	fb = &knl_bootinfo->fb;
	if ( fb->pa == 0 || fb->format == BI_FB_NONE ) {
		return 0;			/* the loader found no screen to draw on */
	}
	if ( fb->format == BI_FB_MASK && !( fb->mask_r == 0x00ff0000 && fb->mask_g == 0x0000ff00
					 && fb->mask_b == 0x000000ff ) ) {
		tm_printf((UB *)"disp: the screen's pixels are laid out %08x %08x %08x; not taken\n",
			  fb->mask_r, fb->mask_g, fb->mask_b);
		return 0;
	}

	hw->width  = fb->width;
	hw->height = fb->height;
	hw->bpp    = 32;
	hw->pitch  = fb->stride;
	hw->format = ( fb->format == BI_FB_RGBX ) ? DISP_FMT_XBGR8888 : DISP_FMT_XRGB8888;
	hw->fb_pa  = fb->pa;
	hw->alpha  = DISP_ALPHA_ANY;
	hw->via    = "UEFI GOP";

	size = (UD)hw->pitch * hw->height;
	base = hw->fb_pa & PAGE_MASK;
	npages = ( hw->fb_pa + size - base + PAGE_SIZE - 1 ) / PAGE_SIZE;
	va = (UBINT)knl_vmap_pa(base, npages, VMAP_NOCACHE);
	if ( va == 0 ) {
		return E_NOMEM;
	}
	knl_fbcon_stop();			/* the screen is the desktop's from here */
	hw->fb_va = va + (UBINT)( hw->fb_pa - base );
	hw->pages = 1;
	hw->flip = NULL;
	hw->vsync = NULL;
	hw->latch_us = 0;
	hw->flip_start = FALSE;
	hw->page2_va = 0;

	return 1;
}

/* The firmware's mode is the one there is */
EXPORT INT knl_disp_hw_setmode( T_DISPHW *hw, UINT w, UINT h )
{
	(void)hw;
	(void)w;
	(void)h;
	return 0;
}

#endif /* X64 */
