/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_disp.c
 *	The screen (design 16.5, stage 9a).
 *
 *	The driver never reads the framebuffer, but a test may: reading the
 *	pixels back is the only way to prove that what was drawn is what
 *	arrived. That is what makes this stage worth doing on the emulated
 *	machine first.
 */

#include "ktest.h"
#include "tstdlib.h"
#include <ts/disp.h>
#include <ts/time.h>
#include <tk/smem.h>
#include "../../device/disp/disp_hw.h"

LOCAL BOOL		have_disp = FALSE;
LOCAL T_DISPSPEC	spec;
LOCAL UB		*fb_all = NULL;	/* the framebuffer, every page of it, mapped to be read */
LOCAL UB		*fb = NULL;	/* the page shown */
LOCAL BOOL		flip_was = FALSE;	/* the screen flipped before the suite turned it off */

/* The first byte of a page */
LOCAL UB *page_base( UINT page )
{
	return fb_all + (UBINT)page * spec.fb_pitch * spec.height;
}

/* One pixel of the back buffer */
LOCAL UW *back_at( INT x, INT y )
{
	UB	*p = (UB *)ts_disp_buffer();

	return (UW *)(p + (UBINT)y * spec.pitch + (UBINT)x * 4);
}

/*
 * The same pixel as a page of the screen holds it, put back into the back
 * buffer's form: blue lowest, and nothing in the top byte, which the
 * send may have set for a screen that reads it as opacity.
 */
LOCAL UW page_at( UB *base, INT x, INT y )
{
	UB	*p = base + (UBINT)y * spec.fb_pitch + (UBINT)x * ( spec.fb_bpp / 8 );
	UW	v;

	if ( spec.fb_format == DISP_FMT_RGB565 ) {
		UH	h = *(volatile UH *)p;

		/* the low bits are lost on the way; they are made up as the send's truncation leaves them */
		return ( (UW)( h & 0xF800 ) << 8 ) | ( (UW)( h & 0x07E0 ) << 5 ) | ( (UW)( h & 0x001F ) << 3 );
	}
	v = *(volatile UW *)p & 0x00FFFFFFU;
	if ( spec.fb_format == DISP_FMT_XBGR8888 ) {
		v = ( v & 0x0000FF00U ) | ( ( v >> 16 ) & 0xFFU ) | ( ( v & 0xFFU ) << 16 );
	}
	return v;
}

/* The same, of the page the screen shows */
LOCAL UW screen_at( INT x, INT y )
{
	return page_at(fb, x, y);
}

/* A colour as the screen can hold it */
LOCAL UW as_sent( UW c )
{
	return ( spec.fb_format == DISP_FMT_RGB565 ) ? ( c & 0x00F8FCF8U ) : ( c & 0x00FFFFFFU );
}

/* the screen is there and says what it is */
LOCAL void test_spec( void )
{
	if ( ts_disp_ref(&spec) < E_OK ) {
		KT_SKIP("the machine has no screen");
	}
	have_disp = TRUE;

	/* the suite reads the page sends go to, which with flipping is not the one shown */
	if ( spec.flipping ) {
		flip_was = TRUE;
		KT_ASSERT_ER(ts_disp_flip(FALSE), E_OK);
		KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
		KT_ASSERT(!spec.flipping);
	}
	KT_ASSERT(spec.fb_pages == 1 || spec.fb_pages == 2);
	KT_ASSERT(spec.fb_shown < spec.fb_pages);

	KT_ASSERT(spec.width >= 320);
	KT_ASSERT(spec.height >= 200);
	KT_ASSERT_EQ(spec.bpp, 32);
	KT_ASSERT_EQ(spec.format, DISP_FMT_XRGB8888);
	KT_ASSERT(spec.pitch >= spec.width * 4);
	KT_ASSERT(spec.fb_pa != 0);
	KT_ASSERT(spec.fb_format == DISP_FMT_XRGB8888 || spec.fb_format == DISP_FMT_XBGR8888
		  || spec.fb_format == DISP_FMT_RGB565);
	KT_ASSERT(spec.fb_pitch >= spec.width * ( spec.fb_bpp / 8 ));
	KT_ASSERT_EQ(spec.held, 0);
	KT_ASSERT(ts_disp_buffer() != NULL);

	tm_printf((UB*)"  screen %d x %d, %d bpp, pitch %d, at %lx\n",
		  (INT)spec.width, (INT)spec.height, (INT)spec.fb_bpp,
		  (INT)spec.fb_pitch, spec.fb_pa);

	/*
	 * A second mapping of the same pixels, so the test can read them.
	 * The driver's own mapping is write only by intent. It is not
	 * cached: on the board a line read once would stay in the cache
	 * and hide every later write that went past it to the memory.
	 */
	fb_all = (UB *)knl_vmap_pa(spec.fb_pa & ~(UD)4095,
				   ((spec.fb_pa & 4095) + (UD)spec.fb_pitch * spec.height * spec.fb_pages
				    + 4095) / 4096,
				   VMAP_NOCACHE);
	KT_ASSERT(fb_all != NULL);
	fb_all += spec.fb_pa & 4095;
	fb = page_base(spec.fb_shown);
}

/* what is drawn into the back buffer reaches the screen */
LOCAL void test_send( void )
{
	T_DISPRECT	r;
	INT		x, y;

	if ( !have_disp ) KT_SKIP("no screen");

	KT_ASSERT_ER(ts_disp_clear(0x00000000), E_OK);

	/* a small block of a colour nothing else uses */
	for ( y = 10; y < 20; y++ ) {
		for ( x = 30; x < 50; x++ ) {
			*back_at(x, y) = 0x00123456;
		}
	}
	r.left = 30; r.top = 10; r.right = 50; r.bottom = 20;
	KT_ASSERT_ER(ts_disp_damage(&r), E_OK);

	/* every pixel of it, and the corners just outside it */
	for ( y = 10; y < 20; y++ ) {
		for ( x = 30; x < 50; x++ ) {
			KT_ASSERT_EQ(screen_at(x, y), as_sent(0x123456));
		}
	}
	KT_ASSERT_EQ(screen_at(29, 10), as_sent(0));
	KT_ASSERT_EQ(screen_at(50, 10), as_sent(0));
	KT_ASSERT_EQ(screen_at(30, 9), as_sent(0));
	KT_ASSERT_EQ(screen_at(30, 20), as_sent(0));
}

/*
 * A rectangle that names only part of what changed sends only that
 * part. This is what makes holding worth doing, so it is worth proving
 * that the driver really is bounded by what it is told.
 */
LOCAL void test_partial( void )
{
	T_DISPRECT	r;
	INT		x;

	if ( !have_disp ) KT_SKIP("no screen");

	for ( x = 100; x < 120; x++ ) {
		*back_at(x, 60) = 0x00ABCDEF;
	}
	/* only the left half is declared */
	r.left = 100; r.top = 60; r.right = 110; r.bottom = 61;
	KT_ASSERT_ER(ts_disp_damage(&r), E_OK);

	for ( x = 100; x < 110; x++ ) {
		KT_ASSERT_EQ(screen_at(x, 60), as_sent(0xABCDEF));
	}
	for ( x = 110; x < 120; x++ ) {
		KT_ASSERT_EQ(screen_at(x, 60), as_sent(0));
	}

	/* and declaring the rest sends the rest */
	r.left = 110; r.right = 120;
	KT_ASSERT_ER(ts_disp_damage(&r), E_OK);
	for ( x = 110; x < 120; x++ ) {
		KT_ASSERT_EQ(screen_at(x, 60), as_sent(0xABCDEF));
	}
}

/*
 * Holding the screen gathers what would have been many sends into one.
 * The count is what the design is about, so the count is what is
 * checked, not only the pixels.
 */
LOCAL void test_hold( void )
{
	T_DISPSTAT	before, after;
	T_DISPRECT	r;
	INT		i;

	if ( !have_disp ) KT_SKIP("no screen");

	KT_ASSERT_ER(ts_disp_clear(0x00000000), E_OK);
	KT_ASSERT_ER(ts_disp_stat(&before), E_OK);

	/* twenty changes, each declared, while the screen is held */
	KT_ASSERT_ER(ts_disp_hold(), E_OK);
	KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
	KT_ASSERT_EQ(spec.held, 1);

	for ( i = 0; i < 20; i++ ) {
		*back_at(200 + i, 100) = 0x00FF0000;
		r.left = 200 + i; r.top = 100;
		r.right = 201 + i; r.bottom = 101;
		KT_ASSERT_ER(ts_disp_damage(&r), E_OK);
	}

	/* nothing has gone across yet */
	KT_ASSERT_ER(ts_disp_stat(&after), E_OK);
	KT_ASSERT_EQ(after.sends, before.sends);
	KT_ASSERT_EQ(screen_at(200, 100), as_sent(0));

	KT_ASSERT_ER(ts_disp_release(), E_OK);

	/* one send covered all twenty */
	KT_ASSERT_ER(ts_disp_stat(&after), E_OK);
	KT_ASSERT_EQ(after.sends, before.sends + 1);
	for ( i = 0; i < 20; i++ ) {
		KT_ASSERT_EQ(screen_at(200 + i, 100), as_sent(0xFF0000));
	}

	/* holds nest: only the last release sends */
	KT_ASSERT_ER(ts_disp_stat(&before), E_OK);
	KT_ASSERT_ER(ts_disp_hold(), E_OK);
	KT_ASSERT_ER(ts_disp_hold(), E_OK);
	*back_at(300, 120) = 0x0000FF00;
	r.left = 300; r.top = 120; r.right = 301; r.bottom = 121;
	KT_ASSERT_ER(ts_disp_damage(&r), E_OK);
	KT_ASSERT_ER(ts_disp_release(), E_OK);
	KT_ASSERT_ER(ts_disp_stat(&after), E_OK);
	KT_ASSERT_EQ(after.sends, before.sends);	/* still held */
	KT_ASSERT_ER(ts_disp_release(), E_OK);
	KT_ASSERT_ER(ts_disp_stat(&after), E_OK);
	KT_ASSERT_EQ(after.sends, before.sends + 1);
	KT_ASSERT_EQ(screen_at(300, 120), as_sent(0x00FF00));
}

/* what is outside the screen is cut away rather than written past it */
LOCAL void test_clip( void )
{
	T_DISPRECT	r;

	if ( !have_disp ) KT_SKIP("no screen");

	/* a rectangle that runs off every edge still works */
	*back_at(0, 0) = 0x00010203;
	r.left = -50; r.top = -50;
	r.right = (INT)spec.width + 50;
	r.bottom = 1;
	KT_ASSERT_ER(ts_disp_damage(&r), E_OK);
	KT_ASSERT_EQ(screen_at(0, 0), as_sent(0x010203));

	/* one entirely outside, and one with no area, are refused */
	r.left = 10; r.top = 10; r.right = 10; r.bottom = 20;
	KT_ASSERT_ER(ts_disp_damage(&r), E_PAR);
	r.left = 10; r.top = 20; r.right = 20; r.bottom = 10;
	KT_ASSERT_ER(ts_disp_damage(&r), E_PAR);
	KT_ASSERT_ER(ts_disp_damage(NULL), E_PAR);
}

/*
 * Red, green and blue, for a person to look at. Reading the pixels back
 * proves only that they arrived as this driver meant them; whether the
 * screen takes the bytes in the order the firmware said is seen on the
 * screen and nowhere else (make DISP_SWAP_RB=1 turns it round).
 */
LOCAL CONST UW	band[3] = { 0x00FF0000, 0x0000FF00, 0x000000FF };

LOCAL void test_colours( void )
{
	T_DISPRECT	r;
	INT		i, x, y;

	if ( !have_disp ) KT_SKIP("no screen");

	KT_ASSERT_ER(ts_disp_clear(0x00000000), E_OK);
	for ( i = 0; i < 3; i++ ) {
		for ( y = 0; y < 96; y++ ) {
			for ( x = 0; x < 96; x++ ) {
				*back_at(i * 112 + x, y) = band[i];
			}
		}
	}
	r.left = 0; r.top = 0; r.right = 3 * 112; r.bottom = 96;
	KT_ASSERT_ER(ts_disp_damage(&r), E_OK);
	for ( i = 0; i < 3; i++ ) {
		KT_ASSERT_EQ(screen_at(i * 112 + 48, 48), as_sent(band[i]));
	}
	tm_printf((UB*)"  look: three squares at the top left, red, green, blue from the left\n");
#if KT_BOARD
	tk_dly_tsk(5000);
#endif
}

/*
 * Two pages. Each send goes to the page not shown, with what the send
 * before it wrote, and that page is then shown: after every send the
 * page shown holds what the back buffer does over what that send and
 * the one before it wrote, and along some whole rows. Two sends of two
 * rectangles far apart show the second page bringing in the first
 * rectangle, which only the other page was sent. A flip on this screen
 * is taken at once, so no send waits. Turned off again, sends go to the
 * page last shown, and the screen is left as the suite found it.
 */
#define FLIP_SENDS	20

LOCAL UW	flip_seed;

LOCAL UW flip_rand( void )
{
	flip_seed = flip_seed * 1103515245U + 12345U;
	return flip_seed >> 8;
}

/* How many pixels of r the page shown has otherwise than the back buffer */
LOCAL INT shown_differs( CONST T_DISPRECT *r )
{
	INT	x, y, n = 0;

	for ( y = r->top; y < r->bottom; y++ ) {
		for ( x = r->left; x < r->right; x++ ) {
			if ( screen_at(x, y) != as_sent(*back_at(x, y)) ) {
				if ( n == 0 ) {
					tm_printf((UB*)"  (%d,%d) on page %d: %06x, meant %06x\n", x, y,
						  (INT)spec.fb_shown, screen_at(x, y), as_sent(*back_at(x, y)));
				}
				n++;
			}
		}
	}
	return n;
}

LOCAL void block( CONST T_DISPRECT *r, UW c )
{
	INT	x, y;

	for ( y = r->top; y < r->bottom; y++ ) {
		for ( x = r->left; x < r->right; x++ ) {
			*back_at(x, y) = c;
		}
	}
}

LOCAL void test_flip( void )
{
	T_DISPSTAT	before, after;
	T_DISPRECT	r[3], was[3], a, b, row;
	INT		k, i, n, nw = 0, w, h;
	UINT		page;
	ER		er;

	if ( !have_disp ) KT_SKIP("no screen");
	er = ts_disp_flip(TRUE);
	if ( er == E_NOSPT ) KT_SKIP("the screen has one page");
	KT_ASSERT_ER(er, E_OK);
	KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
	KT_ASSERT(spec.flipping);
	KT_ASSERT_EQ(spec.fb_pages, 2);
	w = (INT)spec.width;
	h = (INT)spec.height;

	/* the whole screen once, to the other page, which is then shown */
	page = spec.fb_shown;
	KT_ASSERT_ER(ts_disp_stat(&before), E_OK);
	KT_ASSERT_ER(ts_disp_clear(0x00000000), E_OK);
	KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
	KT_ASSERT_EQ(spec.fb_shown, 1 - page);
	fb = page_base(spec.fb_shown);
	KT_ASSERT_ER(ts_disp_stat(&after), E_OK);
	KT_ASSERT_EQ(after.flips, before.flips + 1);

	/* a rectangle sent to one page reaches the other with the next send */
	a.left = 10;  a.top = 10;  a.right = 40;  a.bottom = 30;
	b.left = w - 40;  b.top = h - 30;  b.right = w - 10;  b.bottom = h - 10;
	block(&a, 0x00336699);
	KT_ASSERT_ER(ts_disp_damage(&a), E_OK);
	block(&b, 0x00996633);
	KT_ASSERT_ER(ts_disp_damage(&b), E_OK);
	KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
	fb = page_base(spec.fb_shown);
	KT_ASSERT_EQ(spec.fb_shown, 1 - page);	/* two sends: back where it was */
	KT_ASSERT_EQ(shown_differs(&a), 0);
	KT_ASSERT_EQ(shown_differs(&b), 0);
	KT_ASSERT_EQ(page_at(page_base(page), 20, 20), as_sent(0x336699));
	KT_ASSERT_EQ(page_at(page_base(page), w - 20, h - 20), as_sent(0));

	/* sends of one to three rectangles, of new colours, each under one hold */
	flip_seed = 0x2545F491U;
	KT_ASSERT_ER(ts_disp_stat(&before), E_OK);
	for ( k = 0; k < FLIP_SENDS; k++ ) {
		page = spec.fb_shown;
		n = 1 + (INT)( flip_rand() % 3 );
		KT_ASSERT_ER(ts_disp_hold(), E_OK);
		for ( i = 0; i < n; i++ ) {
			INT	rw = 1 + (INT)( flip_rand() % (UW)( w / 8 ) );
			INT	rh = 1 + (INT)( flip_rand() % (UW)( h / 8 ) );

			r[i].left = (INT)( flip_rand() % (UW)( w - rw + 1 ) );
			r[i].top = (INT)( flip_rand() % (UW)( h - rh + 1 ) );
			r[i].right = r[i].left + rw;
			r[i].bottom = r[i].top + rh;
			block(&r[i], flip_rand() & 0x00FFFFFFU);
			KT_ASSERT_ER(ts_disp_damage(&r[i]), E_OK);
		}
		KT_ASSERT_ER(ts_disp_release(), E_OK);
		KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
		KT_ASSERT_EQ(spec.fb_shown, 1 - page);
		fb = page_base(spec.fb_shown);
		for ( i = 0; i < n; i++ ) {
			KT_ASSERT_EQ(shown_differs(&r[i]), 0);
		}
		for ( i = 0; i < nw; i++ ) {
			KT_ASSERT_EQ(shown_differs(&was[i]), 0);
		}
		row.left = 0;  row.right = w;
		row.top = (INT)( flip_rand() % (UW)h );  row.bottom = row.top + 1;
		KT_ASSERT_EQ(shown_differs(&row), 0);
		for ( i = 0; i < n; i++ ) {
			was[i] = r[i];
		}
		nw = n;
	}
	KT_ASSERT_ER(ts_disp_stat(&after), E_OK);
	KT_ASSERT_EQ(after.flips, before.flips + FLIP_SENDS);
	KT_ASSERT_EQ(after.sends, before.sends + FLIP_SENDS);
#if !KT_BOARD
	KT_ASSERT_EQ(after.waits, before.waits);	/* taken at once */
#endif
	tm_printf((UB*)"  %d sends to the page not shown, %ld kpx copied (with what the page lacked), "
		  "%ld waited for a flip, %ld us all told\n",
		  FLIP_SENDS, (D)( ( after.pixels - before.pixels ) / 1000 ),
		  (D)( after.waits - before.waits ), (D)( after.wait_us - before.wait_us ));

	/* off: sends go to the page last shown */
	page = spec.fb_shown;
	KT_ASSERT_ER(ts_disp_flip(FALSE), E_OK);
	KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
	KT_ASSERT(!spec.flipping);
	KT_ASSERT_EQ(spec.fb_shown, page);
	block(&a, 0x00123456);
	KT_ASSERT_ER(ts_disp_damage(&a), E_OK);
	KT_ASSERT_ER(ts_disp_ref(&spec), E_OK);
	KT_ASSERT_EQ(spec.fb_shown, page);
	KT_ASSERT_EQ(screen_at(20, 20), as_sent(0x123456));

	KT_ASSERT_ER(ts_disp_clear(0x00000000), E_OK);
	if ( flip_was ) {
		KT_ASSERT_ER(ts_disp_flip(TRUE), E_OK);
	}
}

/*
 * The blanks as a clock (design 10.4.5), on a refresh made up for a
 * screen that has none (knl_disp_sim_blank): a send right after a flip
 * waits for the blank that takes it and no longer, and its flip comes
 * in the first quarter of the refresh; a send that comes after the
 * blank that took the flip before, though within a refresh of it, goes
 * on without waiting, where watching the screen would wait a whole
 * refresh more; and sends in a row come one a refresh, asleep until
 * shortly before each blank.
 */
#define BL_US		16667
#define BL_ROW		30

LOCAL void bl_send( INT k )
{
	T_DISPRECT	r;

	r.left = 20 + k % 40;  r.top = 20;  r.right = r.left + 16;  r.bottom = 36;
	block(&r, 0x00102030U + (UW)k);
	(void)ts_disp_damage(&r);
}

LOCAL void test_blank( void )
{
	T_DISPFRAME	f0, f1;
	T_DISPSTAT	s0, s1;
	UD		t0, t1, tc;
	D		since;
	INT		k;
	ER		er;

	if ( !have_disp ) KT_SKIP("no screen");
	er = ts_disp_flip(TRUE);
	if ( er == E_NOSPT ) KT_SKIP("the screen has one page");
	KT_ASSERT_ER(er, E_OK);
	KT_ASSERT_ER(knl_disp_sim_blank(BL_US), E_OK);
	KT_ASSERT_EQ(ts_disp_paced(), BL_US);
	bl_send(0);

	/* right after a flip: until the blank that takes it */
	KT_ASSERT_ER(ts_disp_frames(&f0), E_OK);
	KT_ASSERT_EQ(f0.frame_ns, (UD)BL_US * 1000);
	(void)ts_get_mono(&t0);
	bl_send(1);
	(void)ts_get_mono(&t1);
	KT_ASSERT_ER(ts_disp_frames(&f1), E_OK);
	KT_ASSERT_EQ(f1.waited, f0.waited + 1);
	KT_ASSERT_EQ(f1.taken, f0.taken);
	KT_ASSERT(( t1 - t0 ) / 1000 <= (UD)BL_US + 3000);
	KT_ASSERT_EQ(f1.phase[0], f0.phase[0] + 1);
	KT_ASSERT_EQ(f1.off, f0.off);
	tm_printf((UB *)"  a send right after a flip: %ld us, the flip in quarter %d of the refresh\n",
		  (D)( ( t1 - t0 ) / 1000 ), ( f1.phase[0] > f0.phase[0] ) ? 1 : ( f1.phase[1] > f0.phase[1] ) ? 2 : 3);

	/*
	 * After the blank that took the flip before: the last send's flip
	 * came just after a blank, and the next send's waits for the end of
	 * the next blank, a refresh on; a send 17.8 ms after that flip is
	 * past the blank that took it and still within a refresh and an
	 * eighth of it, where the flip would otherwise be taken to be due
	 */
	bl_send(2);
	(void)ts_get_mono(&tc);
	(void)tk_dly_tsk_u(17800);
	KT_ASSERT_ER(ts_disp_frames(&f0), E_OK);
	KT_ASSERT_ER(ts_disp_stat(&s0), E_OK);
	(void)ts_get_mono(&t0);
	bl_send(3);
	(void)ts_get_mono(&t1);
	KT_ASSERT_ER(ts_disp_frames(&f1), E_OK);
	KT_ASSERT_ER(ts_disp_stat(&s1), E_OK);
	since = (D)( ( t0 - tc ) / 1000 );
	if ( since >= 17100 && since <= 18500 ) {
		KT_ASSERT_EQ(f1.taken, f0.taken + 1);
		KT_ASSERT_EQ(f1.waited, f0.waited);
		KT_ASSERT_EQ(s1.waits, s0.waits);
	}
	tm_printf((UB *)"  a send %ld us after a flip, past its blank: %ld us, %s\n", since,
		  (D)( ( t1 - t0 ) / 1000 ), ( f1.taken > f0.taken ) ? "taken by the clock" : "waited");

	/* in a row: one a refresh, asleep before each blank */
	KT_ASSERT_ER(ts_disp_frames(&f0), E_OK);
	(void)ts_get_mono(&t0);
	for ( k = 0; k < BL_ROW; k++ ) {
		bl_send(4 + k);
	}
	(void)ts_get_mono(&t1);
	KT_ASSERT_ER(ts_disp_frames(&f1), E_OK);
	KT_ASSERT(f1.apart[1] - f0.apart[1] >= BL_ROW - 2);
	KT_ASSERT_EQ(f1.apart[0], f0.apart[0]);
	KT_ASSERT(f1.sleep_us > f0.sleep_us);
	KT_ASSERT(( t1 - t0 ) / 1000 >= (UD)( BL_ROW - 1 ) * BL_US);
	KT_ASSERT(( t1 - t0 ) / 1000 <= (UD)( BL_ROW + 2 ) * BL_US);
	KT_ASSERT_EQ(f1.off, f0.off);
	tm_printf((UB *)"  %d sends in a row: %ld us each; refreshes apart 1:%ld 2:%ld 3+:%ld; "
		  "asleep %ld us, watching %ld us each\n", BL_ROW, (D)( ( t1 - t0 ) / 1000 / BL_ROW ),
		  (D)( f1.apart[1] - f0.apart[1] ), (D)( f1.apart[2] - f0.apart[2] ),
		  (D)( f1.apart[3] + f1.apart[4] - f0.apart[3] - f0.apart[4] ),
		  (D)( ( f1.sleep_us - f0.sleep_us ) / BL_ROW ), (D)( ( f1.spin_us - f0.spin_us ) / BL_ROW ));

	KT_ASSERT_ER(knl_disp_sim_blank(0), E_OK);
	if ( !flip_was ) {
		KT_ASSERT_ER(ts_disp_flip(FALSE), E_OK);
		KT_ASSERT_EQ(ts_disp_paced(), 0U);
	}
	KT_ASSERT_ER(ts_disp_clear(0x00000000), E_OK);
}

EXPORT void ktest_disp( void )
{
	KT_RUN(test_spec);
	KT_RUN(test_send);
	KT_RUN(test_partial);
	KT_RUN(test_hold);
	KT_RUN(test_clip);
	KT_RUN(test_colours);
	KT_RUN(test_flip);
	KT_RUN(test_blank);
}
