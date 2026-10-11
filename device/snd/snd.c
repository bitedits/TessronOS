/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	snd.c
 *	The sound device "snda": channels, conversion, mixing and requests,
 *	apart from the hardware (design 10.15).
 *
 *	Every process that uses the device has a channel of its own for each
 *	unit it uses, found from the process of the task that made the
 *	request (all tasks of a program share it) and the unit: "snda" and
 *	each of "snda0" .. "snda3" are channels apart, so one program can,
 *	say, play on one and record on another. A kernel task that belongs
 *	to no process stands for itself. The device behaves toward each
 *	channel as a device of its own would: the mode, the state, the PCM
 *	buffer, the counts and the stop count are the channel's.
 *
 *	A channel's PCM buffer is a FIFO of the program's samples in the
 *	rate, format (mu-law, 8 bit unsigned, 16 bit signed) and channel
 *	count it set with DN_SDPCMMODE.
 *
 *	Playback: the program fills the FIFO with writes. A stream begins
 *	with the first write after the channel stopped (or was made): one of
 *	data number 0 starts it at the top of the FIFO, dropping what an
 *	earlier stream left unplayed; every later write, before the start or
 *	while playing, goes on after the one before, so a program may queue
 *	its first samples in as many writes as it likes and the bytes it
 *	counts as written are the bytes the channel plays. For every piece of
 *	output the hardware asks for, each playing channel is converted to
 *	the hardware's rate and added in; one short of samples holds its
 *	level. A stream that can wait (a USB packet can go a little later)
 *	gets output only once some channel has the samples it takes, so a
 *	single program's stream follows its writes. At its stop count a
 *	channel adds silence until what was queued has been heard, then
 *	stops; a channel that stays short of samples for a while (an
 *	underrun) stops too. PCM_REPPLAY loops over what was written while
 *	stopped.
 *
 *	Recording: every frame of the hardware's input is converted to the
 *	recording channel's rate, format and channels and put into its FIFO,
 *	which the program drains with reads of data number 0 or 1. It stops
 *	by itself at the stop count or when the FIFO overflows.
 *
 *	Mixing: each playing channel is scaled by its program's volume
 *	(nought when muted), the channels are added, the sum is scaled by the
 *	whole's volume, left and right apart, and what is beyond 16 bits is
 *	bent back softly (soft16) rather than cut, so that loud sums are
 *	rounded off instead of broken into a buzz. A program is its program
 *	object, or its process when it has none; the kernel's own tasks (the
 *	buzzer, the clicks) are one program, "システムの音". Its volume holds
 *	for all its channels (a program that opens several units, as the
 *	browser does for its streams, is turned down as one). The settings
 *	set through DN_SDMIXSET are kept a second after they last change, by
 *	what knl_snd_mix_keeper was given (uconf.c).
 *
 *	Rate conversion: to a lower rate, the mean of the input samples since
 *	the previous output sample; to a higher one, a straight line between
 *	them.
 *
 *	Each FIFO has one writer and one reader, the request and the stream
 *	one way or the other; each moves only its own index, and a channel
 *	is set up completely before its state says it plays or records.
 *
 *	Outputs: every backend names the outputs it has now (sndhw.h). The
 *	stream plays on the one chosen (DN_SDOUTSEL, knl_snd_select) when it
 *	is there and can sound; otherwise, and with "auto" chosen, on the
 *	first that can of the USB audio functions in the order they came,
 *	the I2S interface, HDMI0 and HDMI1. While the stream plays, that
 *	choice is made again every LOOK_MS: an output chosen that came back
 *	(a USB function plugged in again), or one taken out, moves the
 *	stream. A move stops the backend and starts the other; the channels
 *	go on where they were, each converted to the new rate from its next
 *	frame. A backend that stops by itself (its device gone) has the
 *	stream moved at once, and only with nowhere to go do the channels
 *	stop.
 *
 *	A backend may ask for silence first and silence after (sndhw.h,
 *	lead_ms and tail_ms): HDMI, whose screen lets sound through only a
 *	while after it begins. A stream started on it gives silence for
 *	lead_ms before the channels are taken, and once the last channel has
 *	stopped it goes on in silence for tail_ms, so that the next short
 *	sound -- a click, the buzzer -- finds the screen already listening.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <ts/proc.h>
#include <ts/ob.h>
#include "sndhw.h"

IMPORT void knl_usb_lock( void );
IMPORT void knl_usb_unlock( void );
IMPORT UW   knl_usb_ms( void );
IMPORT void knl_usb_kick( void );

#define NCH		16		/* channels in use at once */
#define BUF_DEFAULT	32768
#define BUF_MAX		(1024 * 1024)
#define REAP_MS		1000		/* ended processes are looked for */
#define IDLE_MS		3000		/* among channels unused this long */
#define DRAIN_MS	300		/* output queued ahead of what is heard */
#define STARVE_MS	500		/* a channel short this long stops */
#define LOOK_MS		500		/* how often the output is chosen again while playing */
#define NOUT_ALL	SND_OUT_MAX	/* outputs of all the backends, looked at */

#define MB()		Asm("dmb ish" ::: "memory")

/* why a channel stopped by itself */
#define WHY_ENDCNT	1
#define WHY_OVERRUN	2
#define WHY_UNDERRUN	3

LOCAL CONST UW rate_hz[6] = { 8000, 11025, 16000, 22050, 44100, 48000 };

typedef struct {
	BOOL		used;
	ID		key;		/* the process, or -task for a kernel task */
	INT		unit;		/* the unit it was opened as */
	UB		prog[16];	/* its process's program object, zeros none */
	UW		peak;		/* the loudest it sounded of late, after its volume */
	BOOL		played;		/* it has played: the mixer lists it */
	UW		last_use;
	SDPcmMode	mode;
	volatile INT	ctl;		/* PCM_* */
	INT		bufsz;		/* the ring: one byte more than usable */
	UB		*fifo;
	volatile INT	head, tail;	/* write, read; equal: empty */
	volatile UW	count;		/* bytes made (recording) or taken */
	INT		endcnt;
	volatile BOOL	stop_req;	/* the service is to stop it */
	volatile INT	stop_why;
	/* playback */
	BOOL		top;		/* stopped, no write since: number 0 starts a stream */
	BOOL		rep;		/* repeated play */
	INT		rp;		/* its read position */
	BOOL		pinit;		/* the resampler primed */
	UW		prate;		/* the stream's rate pstep is for */
	UW		pstep, pfrac;	/* program samples per stream sample, 16.16 */
	INT		cur_l, cur_r, nxt_l, nxt_r;
	volatile BOOL	draining;	/* past its stop count */
	INT		drain;		/* stream frames of silence to go */
	volatile BOOL	starved;	/* short of samples at the last mix */
	UW		starve_since;	/* 0 not */
	/* recording */
	UW		step, acc;	/* stream samples per program sample, 16.16 */
	BOOL		down;
	INT		sum_l, sum_r, nsum;
	INT		prev_l, prev_r;
	BOOL		have_prev;
} CHAN;

LOCAL CHAN		ch[NCH];
LOCAL CHAN *volatile	rec_ch = NULL;	/* the channel that records */
LOCAL CONST SNDHW	*play_hw = NULL;	/* the stream that plays */
LOCAL CONST SNDHW	*rec_hw = NULL;	/* and the one that records */
LOCAL UW		mix_rate = 48000;
LOCAL UW		rec_in_rate = 0;
LOCAL INT		rec_in_channels = 0;
LOCAL INT		play_channels = 0;
LOCAL SDStat		snd_stat;
LOCAL ID		snd_devid = 0;
LOCAL BOOL		snd_ready = FALSE;
LOCAL UB		sel_id[SND_ID_MAX] = SND_ID_AUTO;	/* the output chosen */
LOCAL UB		play_id[SND_ID_MAX];	/* the one the stream plays on, "" none */
LOCAL UW		look_ms = 0;		/* the output last chosen again */
LOCAL INT		nmove = 0;		/* the stream moved, since the start */

/*
 * The mixer: the whole's volume and each program's. A program is known
 * by its program object, or, with none, by its process (pid 0 for the
 * kernel's own tasks). An entry is made when a volume is set for it,
 * and the ones of a program object are kept from one start to the next.
 */
#define NVOL		32
#define GAIN_ONE	0x10000		/* a gain of 1, 16.16 */
#define LIM_KNEE	24576		/* above this a sum is bent back toward 32767 */
#define LIM_ROOM	( 32767 - LIM_KNEE )
#define PEAK_FALL	1024		/* a peak falls this much at every mix */

typedef struct {
	BOOL	used;
	UB	prog[16];
	ID	pid;			/* with prog zeros */
	UW	vol;
	BOOL	mute;
} VOLENT;

LOCAL VOLENT		vols[NVOL];
LOCAL UW		mvol_l = SND_VOL_MAX, mvol_r = SND_VOL_MAX;
LOCAL BOOL		mmute = FALSE;
LOCAL UW		mpeak_l, mpeak_r;
LOCAL void		(*mix_keep_fn)( void ) = NULL;
LOCAL volatile BOOL	mix_unkept = FALSE;
LOCAL ID		keep_tsk = 0;

LOCAL void lock( void )   { knl_usb_lock(); }
LOCAL void unlock( void ) { knl_usb_unlock(); }

/* a / b in 16.16 fixed point, for sample rates */
LOCAL UW ratio16( UW a, UW b )
{
	while ( b > 0xffff ) {
		a >>= 1;
		b >>= 1;
	}

	return ((a / b) << 16) + (((a % b) << 16) / b);
}

/* ---------------------------------------------------------------- backends */

/*
 * A recording source for tests: with it on, recording takes whatever
 * knl_snd_capture() is handed, as if it came at 48kHz in stereo.
 */
LOCAL BOOL	loop_on = FALSE;
LOCAL BOOL	loop_run = FALSE;

LOCAL INT  loop_outputs( SDOut *o, INT max ) { return 0; }
LOCAL BOOL loop_can_record( void ) { return loop_on; }
LOCAL ER   loop_play_start( CONST UB *id, UW *rate, INT *channels ) { return E_NOSPT; }
LOCAL void loop_play_stop( void )  { }
LOCAL BOOL loop_play_running( void ) { return FALSE; }
LOCAL void loop_poll( void )       { }

LOCAL ER loop_rec_start( UW rate, UW *dev_rate, INT *channels )
{
	*dev_rate = 48000;
	*channels = 2;
	loop_run = TRUE;

	return E_OK;
}

LOCAL void loop_rec_stop( void )    { loop_run = FALSE; }
LOCAL BOOL loop_rec_running( void ) { return ( loop_run && loop_on ); }
LOCAL ER   loop_get_level( INT dn, SDVol *v )       { return E_NOSPT; }
LOCAL ER   loop_set_level( INT dn, CONST SDVol *v ) { return E_NOSPT; }

LOCAL CONST SNDHW loop_hw = {
	SND_BE_NONE,
	loop_outputs, loop_can_record,
	loop_play_start, loop_play_stop, loop_play_running,
	loop_rec_start, loop_rec_stop, loop_rec_running,
	loop_poll, loop_get_level, loop_set_level, 0, 0
};

EXPORT void knl_snd_loopback( BOOL on )
{
	loop_on = on;
}

/* The backends that play, in the order "auto" takes their outputs */
LOCAL CONST SNDHW *CONST play_hws[] = {
	&knl_uac_hw,
#ifdef RPI5
	&knl_i2s_hw,
	&knl_hdmi_hw,
#endif
};
#define NPLAY_HW	( (INT)( sizeof(play_hws) / sizeof(play_hws[0]) ) )

EXPORT void knl_snd_out_set( SDOut *o, CONST UB *id, CONST UB *name, UW kind, UW flags )
{
	INT	i;

	knl_memset(o, 0, sizeof(*o));
	for ( i = 0; id != NULL && id[i] != 0 && i < SND_ID_MAX - 1; i++ ) {
		o->id[i] = id[i];
	}
	for ( i = 0; name != NULL && name[i] != 0 && i < SND_NAME_MAX - 1; i++ ) {
		o->name[i] = name[i];
	}
	/* a name cut short is cut before the letter that did not fit whole */
	if ( name != NULL && name[i] != 0 ) {
		while ( i > 0 && (name[i] & 0xc0) == 0x80 ) {
			i--;
		}
		o->name[i] = 0;
	}
	o->kind = kind;
	o->flags = flags;
}

LOCAL BOOL same_id( CONST UB *a, CONST UB *b )
{
	return ( knl_strcmp((CONST char *)a, (CONST char *)b) == 0 );
}

LOCAL void id_copy( UB *dst, CONST UB *src )
{
	INT	i;

	for ( i = 0; src[i] != 0 && i < SND_ID_MAX - 1; i++ ) {
		dst[i] = src[i];
	}
	dst[i] = 0;
}

/* Every output of every backend there is now, and the backend of each */
LOCAL INT all_outputs( SDOut *o, CONST SNDHW **hw, INT max )
{
	INT	b, k, n = 0, m;

	for ( b = 0; b < NPLAY_HW && n < max; b++ ) {
		m = play_hws[b]->outputs(o + n, max - n);
		for ( k = 0; k < m; k++ ) {
			hw[n + k] = play_hws[b];
		}
		n += m;
	}

	return n;
}

/*
 * The output chosen among o: the one of its id, else for a USB function
 * one of the same vendor and product plugged in elsewhere; -1 none.
 */
LOCAL INT chosen_of( CONST SDOut *o, INT n )
{
	INT	k, like = -1, i;

	for ( k = 0; k < n; k++ ) {
		if ( same_id(o[k].id, sel_id) ) {
			return k;
		}
		for ( i = 0; o[k].id[i] != 0 && o[k].id[i] == sel_id[i] && o[k].id[i] != '@'; i++ ) {
			;
		}
		if ( like < 0 && o[k].kind == SND_BE_USB && o[k].id[i] == '@' && sel_id[i] == '@' ) {
			like = k;
		}
	}

	return like;
}

/*
 * The hardware to play on, and its output into id: the one chosen when
 * it is there and can sound, else the first of all that can. NULL when
 * none can.
 */
LOCAL CONST SNDHW *pick( UB *id )
{
	SDOut		o[NOUT_ALL];
	CONST SNDHW	*hw[NOUT_ALL];
	INT		n = all_outputs(o, hw, NOUT_ALL), k;

	k = same_id(sel_id, (CONST UB *)SND_ID_AUTO) ? -1 : chosen_of(o, n);
	if ( k < 0 || (o[k].flags & SND_OF_NOSOUND) != 0 ) {
		for ( k = 0; k < n && (o[k].flags & SND_OF_NOSOUND) != 0; k++ ) {
			;
		}
	}
	if ( k >= n ) {
		return NULL;
	}
	if ( id != NULL ) {
		id_copy(id, o[k].id);
	}

	return hw[k];
}

LOCAL CONST SNDHW *hw_play( void )
{
	return pick(NULL);
}

LOCAL CONST SNDHW *hw_record( void )
{
	if ( loop_on ) {
		return &loop_hw;
	}
	if ( knl_uac_hw.can_record() ) {
		return &knl_uac_hw;
	}
#ifdef RPI5
	if ( knl_i2s_hw.can_record() ) {
		return &knl_i2s_hw;
	}
#endif

	return NULL;
}

/* ---------------------------------------------------------------- channels */

LOCAL BOOL playing( CONST CHAN *c )
{
	return ( c->ctl == PCM_PLAY || c->ctl == PCM_REPPLAY );
}

LOCAL BOOL limited( CONST CHAN *c )
{
	return ( c->endcnt < PCM_COUNT_NONE );
}

/* The streams run while a channel needs them */
LOCAL BOOL	keep_on = FALSE;	/* the stream is kept going, in silence when nothing plays */
LOCAL BOOL	lingering = FALSE;	/* no channel plays, the stream goes on in silence */
LOCAL UW	idle_since;		/* since when */
LOCAL BOOL	leading = FALSE;	/* a stream just started: silence until lead_until */
LOCAL UW	lead_until;

/* A stream started on hw: its silence first, when it wants one */
LOCAL void lead_start( CONST SNDHW *hw )
{
	leading = ( hw->lead_ms > 0 );
	lead_until = knl_usb_ms() + hw->lead_ms;
	lingering = FALSE;
}

LOCAL void follow( void )
{
	INT	i;
	BOOL	play = FALSE;

	for ( i = 0; i < NCH; i++ ) {
		if ( ch[i].used && playing(&ch[i]) ) {
			play = TRUE;
		}
	}
	if ( play ) {
		lingering = FALSE;
	} else if ( play_hw != NULL && !lingering && ( keep_on || play_hw->tail_ms > 0 ) ) {
		/* on in silence for a while, or for good: a sound soon after is heard whole */
		lingering = TRUE;
		idle_since = knl_usb_ms();
	} else if ( play_hw != NULL && !keep_on
		 && ( !lingering || (INT)(knl_usb_ms() - idle_since) >= (INT)play_hw->tail_ms ) ) {
		play_hw->play_stop();
		play_hw = NULL;
		play_id[0] = 0;
		lingering = FALSE;
	}
	if ( rec_hw != NULL && rec_ch == NULL ) {
		rec_hw->rec_stop();
		rec_hw = NULL;
	}
}

LOCAL void chan_stop( CHAN *c )
{
	if ( c->stop_why == WHY_UNDERRUN ) {
		snd_stat.underruns++;
	}
	c->ctl = PCM_STOP;
	c->stop_req = FALSE;
	c->draining = FALSE;
	c->top = TRUE;
	if ( rec_ch == c ) {
		rec_ch = NULL;
	}
	follow();
}

LOCAL void chan_free( CHAN *c )
{
	chan_stop(c);
	if ( c->fifo != NULL ) {
		Kfree(c->fifo);
		c->fifo = NULL;
	}
	c->used = FALSE;
}

/* Who is asking: its process, or the task itself outside any process */
LOCAL ID requester( void )
{
	ID	pid = ts_get_pid();

	return ( pid > 0 ) ? pid : -tk_get_tid();
}

LOCAL BOOL requester_gone( ID key )
{
	if ( key > 0 ) {
		T_RPRC	r;

		/*
		 * one ending, or ended and not yet waited for by its parent, is
		 * gone too: it asks nothing of the device any more
		 */
		return ( ts_ref_prc(key, &r) < E_OK || r.state != PS_RUNNING );
	} else {
		T_RTSK	r;

		return ( tk_ref_tsk(-key, &r) < E_OK );
	}
}

/* The requester's channel, set up at its first request (NULL: all taken) */
LOCAL CHAN *chan_of( INT unit, ID key, CONST UB *prog )
{
	CHAN	*c = NULL;
	INT	i;

	for ( i = 0; i < NCH && c == NULL; i++ ) {
		if ( ch[i].used && ch[i].key == key && ch[i].unit == unit ) {
			c = &ch[i];
		}
	}
	for ( i = 0; i < NCH && c == NULL; i++ ) {
		if ( !ch[i].used ) {
			c = &ch[i];
			knl_memset(c, 0, sizeof(*c));
			c->key = key;
			c->unit = unit;
			knl_memcpy(c->prog, prog, sizeof(c->prog));
			c->bufsz = BUF_DEFAULT + 1;
			c->endcnt = PCM_COUNT_NONE;
			c->mode.sampling = PCMST48K;
			c->mode.datafmt = PCMFmt16;
			c->mode.stereo = 1;
			c->top = TRUE;
			c->used = TRUE;
		}
	}
	if ( c != NULL ) {
		c->last_use = knl_usb_ms();
	}

	return c;
}

/* ---------------------------------------------------------------- formats */

LOCAL UB ulaw( INT s )
{
	INT	sign = (s >> 8) & 0x80, exp, mant, mask;

	if ( sign != 0 ) {
		s = -s;
	}
	if ( s > 32635 ) {
		s = 32635;
	}
	s += 0x84;
	for ( exp = 7, mask = 0x4000; exp > 0 && (s & mask) == 0; exp--, mask >>= 1 ) {
		;
	}
	mant = (s >> (exp + 3)) & 0x0f;

	return (UB)~(sign | (exp << 4) | mant);
}

LOCAL INT unulaw( UB u )
{
	INT	s;

	u = (UB)~u;
	s = ((((INT)u & 0x0f) << 3) + 0x84) << ((u >> 4) & 7);
	s -= 0x84;

	return ( (u & 0x80) != 0 ) ? -s : s;
}

LOCAL INT frame_bytes( CONST CHAN *c )
{
	INT	b = ( c->mode.datafmt == PCMFmt16 ) ? 2 : 1;

	return c->mode.stereo ? 2 * b : b;
}

LOCAL INT fifo_used( CONST CHAN *c )
{
	INT	used = c->head - c->tail;

	return ( used < 0 ) ? used + c->bufsz : used;
}

LOCAL ER fifo_alloc( CHAN *c )
{
	if ( c->fifo == NULL ) {
		c->fifo = (UB *)Kmalloc((SZ)c->bufsz);
		if ( c->fifo == NULL ) {
			return E_NOMEM;
		}
	}

	return E_OK;
}

LOCAL INT clamp16( INT s )
{
	return ( s > 32767 ) ? 32767 : ( s < -32768 ) ? -32768 : s;
}

/* ---------------------------------------------------------------- recording */

LOCAL void rec_end( CHAN *c, INT why )
{
	c->ctl = PCM_STOP;
	c->stop_why = why;
	c->stop_req = TRUE;
}

/* One frame into the recording channel's FIFO */
LOCAL void emit( CHAN *c, INT l, INT r )
{
	UB	o[4];
	INT	n = 0, i;

	if ( c->ctl != PCM_REC ) {
		return;
	}
	if ( !c->mode.stereo ) {
		l = (l + r) / 2;
	}
	for ( i = 0; i < ( c->mode.stereo ? 2 : 1 ); i++ ) {
		INT	s = clamp16(( i == 0 ) ? l : r);

		switch ( c->mode.datafmt ) {
		case PCMFmt16:	o[n++] = (UB)s; o[n++] = (UB)(s >> 8); break;
		case PCMFmt8:	o[n++] = (UB)((s >> 8) + 128); break;
		default:	o[n++] = ulaw(s); break;
		}
	}
	if ( limited(c) && c->count + (UW)n > (UW)c->endcnt ) {
		rec_end(c, WHY_ENDCNT);
		return;
	}
	if ( c->bufsz - 1 - fifo_used(c) < n ) {
		rec_end(c, WHY_OVERRUN);
		return;
	}
	for ( i = 0; i < n; i++ ) {
		c->fifo[c->head] = o[i];
		c->head = ( c->head + 1 == c->bufsz ) ? 0 : c->head + 1;
	}
	MB();
	c->count += (UW)n;
	if ( limited(c) && c->count >= (UW)c->endcnt ) {
		rec_end(c, WHY_ENDCNT);
	}
}

/* One frame at the input's rate */
LOCAL void input( CHAN *c, INT l, INT r )
{
	if ( c->down ) {
		c->sum_l += l;
		c->sum_r += r;
		c->nsum++;
		c->acc += 0x10000;
		while ( c->acc >= c->step && c->nsum > 0 ) {
			emit(c, c->sum_l / c->nsum, c->sum_r / c->nsum);
			c->sum_l = c->sum_r = c->nsum = 0;
			c->acc -= c->step;
		}
		return;
	}
	if ( !c->have_prev ) {
		c->prev_l = l;
		c->prev_r = r;
		c->have_prev = TRUE;
		return;
	}
	while ( c->acc <= 0x10000 ) {
		INT	a = (INT)(c->acc >> 2);	/* 14 bits: no overflow below */

		emit(c, c->prev_l + (l - c->prev_l) * a / 0x4000,
		     c->prev_r + (r - c->prev_r) * a / 0x4000);
		c->acc += c->step;
	}
	c->acc -= 0x10000;
	c->prev_l = l;
	c->prev_r = r;
}

EXPORT void knl_snd_capture( CONST UB *src, INT frames, INT channels )
{
	CHAN	*c = rec_ch;
	INT	i, fb = 2 * channels;

	if ( c == NULL || c->ctl != PCM_REC || channels <= 0 || frames <= 0 ) {
		return;
	}
	snd_stat.in_frames += (UW)frames;
	for ( i = 0; i < frames; i++, src += fb ) {
		INT	l = (H)(src[0] | (src[1] << 8));
		INT	r = ( channels > 1 ) ? (H)(src[2] | (src[3] << 8)) : l;

		input(c, l, r);
	}
}

EXPORT void knl_snd_feed( CONST UB *src, INT frames, INT channels )
{
	lock();
	knl_snd_capture(src, frames, channels);
	unlock();
}

/* ---------------------------------------------------------------- playback */

LOCAL void decode( CONST CHAN *c, CONST UB *p, INT *l, INT *r )
{
	INT	i, s[2];

	s[1] = 0;
	for ( i = 0; i < ( c->mode.stereo ? 2 : 1 ); i++ ) {
		switch ( c->mode.datafmt ) {
		case PCMFmt16:	s[i] = (H)(p[0] | (p[1] << 8)); p += 2; break;
		case PCMFmt8:	s[i] = ((INT)p[0] - 128) << 8; p++; break;
		default:	s[i] = unulaw(p[0]); p++; break;
		}
	}
	*l = s[0];
	*r = c->mode.stereo ? s[1] : s[0];
}

/* The channel's next frame; FALSE when there is none */
LOCAL BOOL pull( CHAN *c, INT *l, INT *r )
{
	UB	f[4];
	INT	fb = frame_bytes(c), i;

	if ( c->draining ) {
		return FALSE;
	}
	if ( c->rep ) {
		if ( c->head < fb ) {
			return FALSE;
		}
		knl_memcpy(f, c->fifo + c->rp, fb);
		c->rp += fb;
		if ( c->rp + fb > c->head ) {
			c->rp = 0;
		}
	} else {
		if ( fifo_used(c) < fb ) {
			return FALSE;
		}
		for ( i = 0; i < fb; i++ ) {
			f[i] = c->fifo[c->tail];
			c->tail = ( c->tail + 1 == c->bufsz ) ? 0 : c->tail + 1;
		}
	}
	decode(c, f, l, r);
	c->count += (UW)fb;
	if ( limited(c) && c->count >= (UW)c->endcnt ) {
		c->drain = (INT)(mix_rate / 100 * (UW)DRAIN_MS / 10);
		c->stop_why = WHY_ENDCNT;
		c->draining = TRUE;
	}

	return TRUE;
}

/* One frame of a playing channel at the stream's rate */
LOCAL void next( CHAN *c, INT *l, INT *r )
{
	INT	a;

	if ( c->draining ) {
		*l = *r = 0;
		return;
	}
	a = (INT)(c->pfrac >> 2);
	*l = c->cur_l + (c->nxt_l - c->cur_l) * a / 0x4000;
	*r = c->cur_r + (c->nxt_r - c->cur_r) * a / 0x4000;
	c->pfrac += c->pstep;
	while ( c->pfrac >= 0x10000 ) {
		c->cur_l = c->nxt_l;
		c->cur_r = c->nxt_r;
		pull(c, &c->nxt_l, &c->nxt_r);	/* short: the level is held */
		c->pfrac -= 0x10000;
	}
}

/* The channel has what frames stream frames take, or needs nothing */
LOCAL BOOL chan_ready( CONST CHAN *c, INT frames )
{
	INT	pulls, have;

	if ( c->draining || c->rep ) {
		return TRUE;
	}
	pulls = (INT)((c->pfrac + (UW)frames * c->pstep) >> 16);
	have = fifo_used(c);
	/* all of it up to the stop count is in: the end, silence after it */

	return ( have >= pulls * frame_bytes(c)
	      || ( limited(c) && c->count + (UW)have >= (UW)c->endcnt ) );
}

/* ---------------------------------------------------------------- the mixer */

LOCAL BOOL prog_none( CONST UB *prog )
{
	INT	i;

	for ( i = 0; i < 16; i++ ) {
		if ( prog[i] != 0 ) {
			return FALSE;
		}
	}
	return TRUE;
}

LOCAL BOOL prog_same( CONST UB *a, CONST UB *b )
{
	INT	i;

	for ( i = 0; i < 16 && a[i] == b[i]; i++ ) {
		;
	}
	return ( i == 16 );
}

/* A program object's name, cut at a whole letter */
LOCAL BOOL prog_name( CONST UB *prog, UB *name )
{
	T_OBREF	*r = (T_OBREF *)Kmalloc(sizeof(T_OBREF));
	TS_UUID	u;
	INT	n = 0;

	if ( r == NULL ) {
		return FALSE;
	}
	knl_memcpy(&u, prog, 16);
	if ( ob_ref_obj(&u, r) >= E_OK ) {
		while ( n < SND_NAME_MAX - 1 && n < OB_NAME_MAX && r->name[n] != 0 ) {
			name[n] = r->name[n];
			n++;
		}
		while ( n > 0 && n < OB_NAME_MAX && r->name[n] != 0 && ( r->name[n] & 0xC0 ) == 0x80 ) {
			n--;
		}
		name[n] = 0;
	}
	Kfree(r);
	return ( n > 0 );
}

/* The process a channel's program is known by when it has no object: 0 the kernel's own */
LOCAL ID chan_pid( CONST CHAN *c )
{
	return ( c->key > 0 ) ? c->key : 0;
}

/* The entry of a program (prog, or pid when prog is zeros); made when make and there is room */
LOCAL VOLENT *vol_of( CONST UB *prog, ID pid, BOOL make )
{
	BOOL	none = prog_none(prog);
	VOLENT	*free = NULL;
	INT	i;

	for ( i = 0; i < NVOL; i++ ) {
		VOLENT	*v = &vols[i];

		if ( !v->used ) {
			if ( free == NULL ) free = v;
			continue;
		}
		if ( none ? ( prog_none(v->prog) && v->pid == pid )
			  : ( prog_same(v->prog, prog) ) ) {
			return v;
		}
	}
	if ( !make || free == NULL ) {
		return NULL;
	}
	knl_memset(free, 0, sizeof(*free));
	knl_memcpy(free->prog, prog, 16);
	free->pid = none ? pid : 0;
	free->vol = SND_VOL_MAX;
	free->used = TRUE;
	return free;
}

/* A volume as a gain, 16.16: its square */
LOCAL INT gain_of( UW vol )
{
	if ( vol > SND_VOL_MAX ) vol = SND_VOL_MAX;
	return (INT)( (UD)vol * vol * GAIN_ONE / ( SND_VOL_MAX * SND_VOL_MAX ) );
}

LOCAL INT chan_gain( CONST CHAN *c )
{
	VOLENT	*v = vol_of(c->prog, chan_pid(c), FALSE);

	return ( v == NULL ) ? GAIN_ONE : v->mute ? 0 : gain_of(v->vol);
}

/*
 * A sum held to 16 bits without cutting it: below the knee as it is,
 * above it bent so that it comes ever nearer 32767 and never reaches it
 * -- y = knee + room * e / (e + room), e what is over the knee -- which
 * runs on from the straight part with the same slope.
 */
LOCAL INT soft16( D x )
{
	D	a = ( x < 0 ) ? -x : x, e;

	if ( a <= LIM_KNEE ) {
		return (INT)x;
	}
	e = a - LIM_KNEE;
	a = LIM_KNEE + ( (D)LIM_ROOM * e ) / ( e + LIM_ROOM );
	return (INT)( ( x < 0 ) ? -a : a );
}

#ifdef USE_KTEST
#define TAP_FRAMES	4096
LOCAL H		tap[TAP_FRAMES * 2];
LOCAL INT	tap_at = 0;			/* the next frame written */

EXPORT INT knl_snd_tap( H *dst, INT frames )
{
	INT	i, at;

	lock();
	if ( frames > TAP_FRAMES ) frames = TAP_FRAMES;
	at = ( tap_at - frames + TAP_FRAMES ) % TAP_FRAMES;
	for ( i = 0; i < frames; i++ ) {
		dst[i * 2] = tap[at * 2];
		dst[i * 2 + 1] = tap[at * 2 + 1];
		at = ( at + 1 ) % TAP_FRAMES;
	}
	unlock();
	return frames;
}

EXPORT void knl_snd_tag( INT unit, CONST UB *prog )
{
	ID	key = requester();
	INT	i;

	lock();
	for ( i = 0; i < NCH; i++ ) {
		if ( ch[i].used && ch[i].key == key && ch[i].unit == unit ) {
			knl_memcpy(ch[i].prog, prog, 16);
		}
	}
	unlock();
}
#endif

EXPORT INT knl_snd_mix( UB *dst, INT frames, UW rate, INT channels, BOOL wait )
{
	CHAN	*act[NCH];
	INT	gain[NCH];
	INT	n = 0, i, k, o = 0, gl, gr;
	BOOL	ready = FALSE;

	if ( frames <= 0 || rate == 0 || channels <= 0 ) {
		return 0;
	}
	mix_rate = rate;
	if ( leading ) {
		if ( (INT)(knl_usb_ms() - lead_until) < 0 ) {
			/* the stream's silence first: the channels wait where they are */
			knl_memset(dst, 0, frames * channels * 2);
			snd_stat.out_frames += (UW)frames;
			return frames;
		}
		leading = FALSE;
	}
	for ( i = 0; i < NCH; i++ ) {
		CHAN	*c = &ch[i];

		if ( !c->used || !playing(c) ) {
			continue;
		}
		if ( c->pinit && c->prate != rate ) {
			/* the stream moved to another output: the same samples at its rate */
			c->pstep = ratio16(rate_hz[c->mode.sampling], rate);
			c->prate = rate;
		}
		if ( !c->pinit ) {
			c->pstep = ratio16(rate_hz[c->mode.sampling], rate);
			c->prate = rate;
			c->pfrac = 0;
			c->cur_l = c->cur_r = c->nxt_l = c->nxt_r = 0;
			if ( pull(c, &c->cur_l, &c->cur_r) ) {
				pull(c, &c->nxt_l, &c->nxt_r);
			}
			c->pinit = TRUE;
		}
		c->starved = !chan_ready(c, frames);
		if ( !c->starved ) {
			ready = TRUE;
		}
		gain[n] = chan_gain(c);
		act[n++] = c;
	}
	snd_stat.out_packets++;
	if ( n == 0 ) {
		ready = TRUE;			/* no channel at all: silence, the stream kept going */
	}
	if ( wait && !ready ) {
		return 0;
	}
	if ( !ready ) {
		snd_stat.out_short++;
	}
	gl = mmute ? 0 : gain_of(mvol_l);
	gr = mmute ? 0 : gain_of(mvol_r);
	for ( k = 0; k < n; k++ ) {
		act[k]->peak = ( act[k]->peak > PEAK_FALL ) ? act[k]->peak - PEAK_FALL : 0;
	}
	mpeak_l = ( mpeak_l > PEAK_FALL ) ? mpeak_l - PEAK_FALL : 0;
	mpeak_r = ( mpeak_r > PEAK_FALL ) ? mpeak_r - PEAK_FALL : 0;
	for ( i = 0; i < frames; i++ ) {
		INT	sl = 0, sr = 0, l, r;

		/* each channel by its program's volume, then added */
		for ( k = 0; k < n; k++ ) {
			next(act[k], &l, &r);
			l = (INT)( ( (D)l * gain[k] ) >> 16 );
			r = (INT)( ( (D)r * gain[k] ) >> 16 );
			sl += l;
			sr += r;
			if ( (UW)( l < 0 ? -l : l ) > act[k]->peak ) act[k]->peak = (UW)( l < 0 ? -l : l );
			if ( (UW)( r < 0 ? -r : r ) > act[k]->peak ) act[k]->peak = (UW)( r < 0 ? -r : r );
		}
		/* the sum by the whole's volume, left and right apart, and held to 16 bits */
		sl = soft16(( (D)sl * gl ) >> 16);
		sr = soft16(( (D)sr * gr ) >> 16);
		if ( (UW)( sl < 0 ? -sl : sl ) > mpeak_l ) mpeak_l = (UW)( sl < 0 ? -sl : sl );
		if ( (UW)( sr < 0 ? -sr : sr ) > mpeak_r ) mpeak_r = (UW)( sr < 0 ? -sr : sr );
#ifdef USE_KTEST
		tap[tap_at * 2] = (H)sl;
		tap[tap_at * 2 + 1] = (H)sr;
		tap_at = ( tap_at + 1 ) % TAP_FRAMES;
#endif
		if ( channels == 1 ) {
			INT	m = (sl + sr) / 2;

			dst[o++] = (UB)m;
			dst[o++] = (UB)(m >> 8);
		} else {
			dst[o++] = (UB)sl;
			dst[o++] = (UB)(sl >> 8);
			dst[o++] = (UB)sr;
			dst[o++] = (UB)(sr >> 8);
			for ( k = 2; k < channels; k++ ) {
				dst[o++] = 0;
				dst[o++] = 0;
			}
		}
	}
	for ( k = 0; k < n; k++ ) {
		CHAN	*c = act[k];

		if ( c->draining && !c->stop_req && (c->drain -= frames) <= 0 ) {
			c->stop_req = TRUE;
		}
	}
	snd_stat.out_frames += (UW)frames;

	return frames;
}

/* ---------------------------------------------------------------- control */

LOCAL ER start_rec( CHAN *c )
{
	UW	out = rate_hz[c->mode.sampling], in = 0;
	INT	nch = 0;
	CONST SNDHW *hw;
	ER	er;

	if ( rec_ch != NULL && rec_ch != c ) {
		return E_BUSY;			/* another program records */
	}
	hw = hw_record();
	if ( hw == NULL ) {
		return E_NOSPT;
	}
	er = fifo_alloc(c);
	if ( er < E_OK ) {
		return er;
	}
	/* rec_ch == c: its recording ended and was not stopped yet */
	rec_ch = NULL;
	if ( rec_hw != NULL ) {
		rec_hw->rec_stop();
		rec_hw = NULL;
	}
	c->head = c->tail = 0;
	c->count = 0;
	c->stop_req = FALSE;
	c->stop_why = 0;
	er = hw->rec_start(out, &in, &nch);
	if ( er < E_OK ) {
		return er;
	}
	rec_hw = hw;
	if ( in == 0 ) {
		in = out;
	}
	rec_in_rate = in;
	rec_in_channels = nch;
	c->down = ( in >= out );
	c->step = ratio16(in, out);
	c->acc = 0;
	c->sum_l = c->sum_r = c->nsum = 0;
	c->have_prev = FALSE;
	MB();
	c->ctl = PCM_REC;
	rec_ch = c;

	return E_OK;
}

LOCAL ER start_play( CHAN *c, INT how )
{
	ER	er;

	UB	id[SND_ID_MAX];

	if ( play_hw == NULL && hw_play() == NULL ) {
		return E_NOSPT;
	}
	er = fifo_alloc(c);
	if ( er < E_OK ) {
		return er;
	}
	c->count = 0;
	c->stop_req = FALSE;
	c->stop_why = 0;
	c->draining = FALSE;
	c->rp = 0;
	c->rep = ( how == PCM_REPPLAY );
	c->pinit = FALSE;			/* primed at its first mix */
	c->starved = FALSE;
	c->starve_since = 0;
	c->top = FALSE;				/* what is in the FIFO is this stream's */
	c->played = TRUE;
	MB();
	c->ctl = how;
	if ( play_hw == NULL ) {
		CONST SNDHW	*hw = pick(id);
		UW		rate = 0;

		if ( hw != NULL ) {
			lead_start(hw);		/* before the start: a backend fills its ring as it starts */
		}
		er = ( hw != NULL ) ? hw->play_start(id, &rate, &play_channels) : E_NOSPT;
		if ( er < E_OK ) {
			leading = FALSE;
			c->ctl = PCM_STOP;
			c->top = TRUE;
			return er;
		}
		play_hw = hw;
		mix_rate = rate;
		id_copy(play_id, id);
		look_ms = knl_usb_ms();
		/* the manager's task may be asleep for long: it is to serve the stream from now on */
		knl_usb_kick();
	}

	return E_OK;
}

/*
 * The stream onto the output the choice now gives, from hw (NULL: it
 * was lost) -- the channels playing on -- or, with nowhere to go, the
 * channels that play stopped. TRUE when it plays.
 */
LOCAL BOOL move_stream( CONST SNDHW *old )
{
	CONST SNDHW	*hw;
	UB		id[SND_ID_MAX], was[SND_ID_MAX];
	UW		rate = 0;
	INT		i;
	ER		er = E_NOSPT;

	id_copy(was, play_id);
	if ( old != NULL ) {
		old->play_stop();
	}
	play_hw = NULL;
	play_id[0] = 0;
	hw = pick(id);
	if ( hw != NULL ) {
		lead_start(hw);
		er = hw->play_start(id, &rate, &play_channels);
	}
	nmove++;
	tm_printf((UB *)"TessronOS: snd: the stream moves from %s to %s (%d)\n",
		  ( was[0] != 0 ) ? (CONST char *)was : "nothing",
		  ( hw != NULL ) ? (CONST char *)id : "nothing", (INT)er);
	if ( er >= E_OK ) {
		play_hw = hw;
		mix_rate = rate;
		id_copy(play_id, id);
		knl_usb_kick();
		return TRUE;
	}
	leading = FALSE;
	for ( i = 0; i < NCH; i++ ) {
		if ( ch[i].used && playing(&ch[i]) ) {
			ch[i].ctl = PCM_STOP;
			ch[i].top = TRUE;
		}
	}

	return FALSE;
}

/* While the stream plays: the output the choice gives now, when that is another */
LOCAL void choose_again( void )
{
	UB	id[SND_ID_MAX];

	if ( play_hw == NULL ) {
		return;
	}
	look_ms = knl_usb_ms();
	if ( pick(id) != NULL && !same_id(id, play_id) ) {
		(void)move_stream(play_hw);
	}
}

/*
 * Kept going (keep_on) with no stream: the stream started on what the
 * choice gives, in silence until something plays, with the lock held.
 */
LOCAL void keep_start( void )
{
	CONST SNDHW	*hw;
	UB		id[SND_ID_MAX];
	UW		rate = 0;

	if ( !keep_on || play_hw != NULL ) {
		return;
	}
	hw = pick(id);
	if ( hw == NULL ) {
		return;
	}
	lead_start(hw);
	if ( hw->play_start(id, &rate, &play_channels) < E_OK ) {
		leading = FALSE;
		return;
	}
	play_hw = hw;
	mix_rate = rate;
	id_copy(play_id, id);
	lingering = TRUE;
	idle_since = knl_usb_ms();
	look_ms = idle_since;
	knl_usb_kick();
}

/* An id of a form an output has */
LOCAL BOOL id_ok( CONST UB *id )
{
	INT	n = (INT)knl_strlen((CONST char *)id);

	if ( n <= 0 || n >= SND_ID_MAX ) {
		return FALSE;
	}

	return ( same_id(id, (CONST UB *)SND_ID_AUTO) || same_id(id, (CONST UB *)"i2s")
	      || same_id(id, (CONST UB *)"hdmi0") || same_id(id, (CONST UB *)"hdmi1")
	      || ( n > 5 && id[0] == 'u' && id[1] == 's' && id[2] == 'b' && id[3] == ':' ) );
}

EXPORT ER knl_snd_select( CONST UB *id )
{
	if ( id == NULL || !id_ok(id) ) {
		return E_PAR;
	}
	if ( !snd_ready ) {
		id_copy(sel_id, id);
		return E_OK;
	}
	lock();
	if ( !same_id(sel_id, id) ) {
		id_copy(sel_id, id);
		tm_printf((UB *)"TessronOS: snd: output %s chosen\n", (CONST char *)sel_id);
	}
	choose_again();
	keep_start();
	unlock();

	return E_OK;
}

EXPORT ER knl_snd_keep( BOOL on )
{
	if ( !snd_ready ) {
		keep_on = on;
		return E_OK;
	}
	lock();
	if ( keep_on != on ) {
		keep_on = on;
		tm_printf((UB *)"TessronOS: snd: the stream %s\n", on ? "kept going" : "stops when nothing plays");
	}
	keep_start();
	follow();
	unlock();
	knl_usb_kick();

	return E_OK;
}

EXPORT BOOL knl_snd_keeping( void )
{
	return keep_on;
}

/* The outputs with "auto" first, and what is chosen and plays marked, with lock held */
LOCAL INT outputs_of( SDOut *o, INT max )
{
	CONST SNDHW	*hw[NOUT_ALL];
	SDOut		a[NOUT_ALL];
	INT		n, k, m = 0, c;

	if ( max <= 0 ) {
		return 0;
	}
	n = all_outputs(a, hw, NOUT_ALL);
	c = same_id(sel_id, (CONST UB *)SND_ID_AUTO) ? -1 : chosen_of(a, n);
	knl_snd_out_set(&o[m], (CONST UB *)SND_ID_AUTO, (CONST UB *)"", SND_BE_NONE,
			same_id(sel_id, (CONST UB *)SND_ID_AUTO) ? SND_OF_CHOSEN : 0);
	m++;
	for ( k = 0; k < n && m < max; k++, m++ ) {
		o[m] = a[k];
		if ( k == c ) {
			o[m].flags |= SND_OF_CHOSEN;
		}
		if ( play_hw != NULL && same_id(a[k].id, play_id) ) {
			o[m].flags |= SND_OF_PLAYING;
			o[m].rate = mix_rate;
			o[m].channels = (UW)play_channels;
		}
	}

	return m;
}

/* ---------------------------------------------------------------- the mixer's settings */

LOCAL volatile UW	mix_gen = 0;		/* changes to be kept, counted */

/*
 * The keeper's task: once the settings have been still for a second
 * (a volume dragged is many changes), they are kept, outside every lock
 * of this device.
 */
LOCAL void keep_task( INT stacd, void *exinf )
{
	UW	g;

	(void)stacd;
	(void)exinf;
	for ( ;; ) {
		(void)tk_slp_tsk(TMO_FEVR);
		do {
			g = mix_gen;
			tk_dly_tsk(1000);
		} while ( g != mix_gen );
		mix_unkept = FALSE;
		if ( mix_keep_fn != NULL ) {
			mix_keep_fn();
		}
	}
}

EXPORT void knl_snd_mix_keeper( void (*fn)( void ) )
{
	mix_keep_fn = fn;
}

LOCAL void keep_soon( void )
{
	mix_gen++;
	mix_unkept = TRUE;
	if ( keep_tsk <= 0 ) {
		T_CTSK	ct;

		knl_memset(&ct, 0, sizeof(ct));
		ct.tskatr = TA_HLNG | TA_RNG0;
		ct.task = (FP)keep_task;
		ct.itskpri = 30;
		ct.stksz = 8 * 1024;
		keep_tsk = tk_cre_tsk(&ct);
		if ( keep_tsk <= 0 || tk_sta_tsk(keep_tsk, 0) < E_OK ) {
			keep_tsk = 0;
			return;
		}
	}
	(void)tk_wup_tsk(keep_tsk);
}

EXPORT ER knl_snd_mix_set( CONST SDMixSet *s, BOOL keep )
{
	VOLENT	*v;
	ER	er = E_OK;

	if ( s == NULL || s->lv > SND_VOL_MAX || s->rv > SND_VOL_MAX ) {
		return E_PAR;
	}
	lock();
	if ( s->what == SND_MIX_ALL ) {
		mvol_l = s->lv;
		mvol_r = s->rv;
		mmute = ( s->mute != 0 );
	} else if ( s->what == SND_MIX_APP ) {
		v = vol_of(s->prog, ( s->pid > 0 ) ? s->pid : 0, TRUE);
		if ( v == NULL ) {
			er = E_LIMIT;
		} else {
			v->vol = s->lv;
			v->mute = ( s->mute != 0 );
		}
	} else {
		er = E_PAR;
	}
	unlock();
	if ( er >= E_OK && keep ) {
		keep_soon();
	}
	return er;
}

/* The whole, and the programs that have channels now, with the lock held */
LOCAL INT mix_of( SDMix *m )
{
	INT	i, k;

	knl_memset(m, 0, sizeof(*m));
	m->lv = mvol_l;
	m->rv = mvol_r;
	m->mute = mmute;
	m->level_l = mpeak_l;
	m->level_r = mpeak_r;
	for ( i = 0; i < NCH; i++ ) {
		CHAN		*c = &ch[i];
		SDMixApp	*a = NULL;
		VOLENT		*v;
		ID		pid = chan_pid(c);

		if ( !c->used || !c->played ) {
			continue;		/* one only read or set, as a mixer's own is */
		}
		for ( k = 0; k < (INT)m->napp && a == NULL; k++ ) {
			if ( prog_none(c->prog) ? ( prog_none(m->app[k].prog) && m->app[k].pid == pid )
						: prog_same(m->app[k].prog, c->prog) ) {
				a = &m->app[k];
			}
		}
		if ( a == NULL ) {
			if ( m->napp >= SND_APP_MAX ) {
				continue;
			}
			a = &m->app[m->napp++];
			knl_memcpy(a->prog, c->prog, 16);
			a->pid = pid;
			v = vol_of(c->prog, pid, FALSE);
			a->vol = ( v != NULL ) ? v->vol : SND_VOL_MAX;
			a->mute = ( v != NULL ) ? v->mute : FALSE;
		}
		a->channels++;
		if ( playing(c) ) {
			a->playing++;
		}
		if ( c->peak > a->level ) {
			a->level = c->peak;
		}
	}
	return (INT)m->napp;
}

EXPORT INT knl_snd_mix_get( SDMix *m )
{
	INT	n;

	lock();
	n = mix_of(m);
	unlock();
	return n;
}

/* The settings of the programs that have an object, for the keeper: how many */
EXPORT INT knl_snd_mix_kept( SDMixSet *s, INT max )
{
	INT	i, n = 0;

	lock();
	for ( i = 0; i < NVOL && n < max; i++ ) {
		if ( vols[i].used && !prog_none(vols[i].prog) ) {
			knl_memset(&s[n], 0, sizeof(s[n]));
			s[n].what = SND_MIX_APP;
			knl_memcpy(s[n].prog, vols[i].prog, 16);
			s[n].lv = s[n].rv = vols[i].vol;
			s[n].mute = vols[i].mute;
			n++;
		}
	}
	unlock();
	return n;
}

EXPORT INT knl_snd_outputs( SDOut *o, INT max )
{
	INT	n;

	lock();
	n = outputs_of(o, max);
	unlock();

	return n;
}

/*
 * Every few milliseconds from the USB manager's task, with its lock
 * held: the backends are served, a stream the hardware lost stops its
 * channels, and channels that ended, underran or whose process ended
 * are stopped or let go.
 */
EXPORT void knl_snd_poll( void )
{
	LOCAL UW	last_reap = 0;
	UW		now;
	INT		i;

	if ( !snd_ready ) {
		return;
	}
	now = knl_usb_ms();
	if ( play_hw == NULL && keep_on && (INT)(now - look_ms) >= LOOK_MS ) {
		look_ms = now;
		keep_start();			/* an output came: the stream on it, in silence */
	}
	if ( play_hw != NULL ) {
		play_hw->poll();
		if ( !play_hw->play_running() ) {
			/* its device went: on to what is there, the channels playing on */
			(void)move_stream(play_hw);
		} else if ( (INT)(now - look_ms) >= LOOK_MS ) {
			choose_again();
		}
	}
	if ( rec_hw != NULL ) {
		if ( rec_hw != play_hw ) {
			rec_hw->poll();
		}
		if ( !rec_hw->rec_running() ) {
			rec_hw = NULL;
			if ( rec_ch != NULL ) {
				rec_ch->ctl = PCM_STOP;
				rec_ch = NULL;
			}
		}
	}
	for ( i = 0; i < NCH; i++ ) {
		CHAN	*c = &ch[i];

		if ( !c->used ) {
			continue;
		}
		if ( c->ctl == PCM_PLAY && !c->draining ) {
			if ( !c->starved ) {
				c->starve_since = 0;
			} else if ( c->starve_since == 0 ) {
				c->starve_since = now | 1;
			} else if ( (INT)(now - c->starve_since) >= STARVE_MS ) {
				c->stop_why = WHY_UNDERRUN;
				c->stop_req = TRUE;
			}
		}
		if ( c->stop_req ) {
			chan_stop(c);
		}
	}
	/* channels of programs that have ended, and idle ones of kernel tasks */
	if ( (INT)(now - last_reap) >= REAP_MS ) {
		last_reap = now;
		for ( i = 0; i < NCH; i++ ) {
			CHAN	*c = &ch[i];

			if ( !c->used || (INT)(now - c->last_use) < IDLE_MS ) {
				continue;
			}
			if ( requester_gone(c->key) || ( c->key < 0 && c->ctl == PCM_STOP ) ) {
				chan_free(c);
			}
		}
		/* the volume of a process with no program object goes with it */
		for ( i = 0; i < NVOL; i++ ) {
			if ( vols[i].used && vols[i].pid > 0 && prog_none(vols[i].prog) && requester_gone(vols[i].pid) ) {
				vols[i].used = FALSE;
			}
		}
	}
	follow();
}

EXPORT BOOL knl_snd_active( void )
{
	/* kept going: looked at often, so that an output that comes is taken at once */
	return ( play_hw != NULL || rec_hw != NULL || keep_on );
}

/*
 * From a task that waits for a USB transfer with the manager's lock held:
 * a stream that does not ride on USB transfers (the I2S) is served
 * meanwhile, since the manager's task cannot take the lock to do it.
 * The USB audio stream is left alone: it moves with the controller's
 * events, which the wait serves anyway.
 */
EXPORT void knl_snd_service( void )
{
	if ( snd_ready && play_hw != NULL && play_hw->kind != SND_BE_USB ) {
		play_hw->poll();
	}
}

/* ---------------------------------------------------------------- requests */

LOCAL BOOL is_level( INT dn )
{
	return ( dn <= DN_SDGAININ && dn >= DN_SDVOLAUX2 );
}

/* An attribute answered: what fits of it, or its size to a read of 0 */
LOCAL void reply( T_DEVREQ *req, CONST void *p, INT size )
{
	if ( req->size <= 0 ) {
		req->asize = size;
		return;
	}
	if ( req->size < size ) {
		size = (INT)req->size;
	}
	knl_memcpy(req->buf, p, size);
	req->asize = size;
}

LOCAL ER proc_read( CHAN *c, T_DEVREQ *req )
{
	ER	er;

	switch ( req->start ) {
	case 0:
	case 1: {
		/* recorded bytes, out of the ring in up to two pieces */
		INT	n = 0, want = (INT)req->size, h = c->head;

		if ( playing(c) || c->fifo == NULL ) {
			return E_OK;
		}
		if ( want <= 0 ) {
			req->asize = fifo_used(c);
			return E_OK;
		}
		while ( n < want && c->tail != h ) {
			INT	end = ( h > c->tail ) ? h : c->bufsz;
			INT	k = end - c->tail;

			if ( k > want - n ) {
				k = want - n;
			}
			knl_memcpy((UB *)req->buf + n, c->fifo + c->tail, k);
			n += k;
			c->tail = ( c->tail + k == c->bufsz ) ? 0 : c->tail + k;
		}
		req->asize = n;
		return E_OK;
	}
	case DN_SDPCMINFO: {
		SDPcmInfo	in;
		BOOL		rec, play;
		INT		i;

		lock();
		rec = ( hw_record() != NULL );
		play = ( play_hw != NULL || hw_play() != NULL );
		unlock();
		knl_memset(&in, 0, sizeof(in));
		if ( rec || play ) {
			for ( i = PCMST8K; i <= PCMST48K; i++ ) {
				in.spt[i].datafmt = (1 << PCMFmt8ulaw) | (1 << PCMFmt8)
						  | (1 << PCMFmt16);
				in.spt[i].mrecord = in.spt[i].srecord = rec;
				in.spt[i].mplay = in.spt[i].splay = play;
			}
		}
		reply(req, &in, sizeof(in));
		return E_OK;
	}
	case DN_SDPCMMODE:
		reply(req, &c->mode, sizeof(c->mode));
		return E_OK;
	case DN_SDPCMCTL: {
		W	v = c->ctl;

		reply(req, &v, sizeof(v));
		return E_OK;
	}
	case DN_SDPCMCNT: {
		W	v = (W)c->count;

		reply(req, &v, sizeof(v));
		return E_OK;
	}
	case DN_SDPCMENDCNT: {
		W	v = c->endcnt;

		reply(req, &v, sizeof(v));
		return E_OK;
	}
	case DN_SDPCMBUFSZ: {
		W	v = c->bufsz - 1;

		reply(req, &v, sizeof(v));
		return E_OK;
	}
	case DN_SDSELIN:
	case DN_SDSELOUT: {
		SDSel	s;

		knl_memset(&s, 0, sizeof(s));
		lock();
		if ( req->start == DN_SDSELIN ) {
			s.mic = ( hw_record() != NULL );
		} else {
			s.pcm = ( play_hw != NULL || hw_play() != NULL );
		}
		unlock();
		reply(req, &s, sizeof(s));
		return E_OK;
	}
	case DN_SDOUTS: {
		SDOut	o[SND_OUT_MAX];
		INT	n;

		lock();
		n = outputs_of(o, SND_OUT_MAX);
		unlock();
		reply(req, o, n * (INT)sizeof(SDOut));
		return E_OK;
	}
	case DN_SDMIXER: {
		SDMix	*m = (SDMix *)Kmalloc(sizeof(SDMix));
		INT	k;

		if ( m == NULL ) {
			return E_NOMEM;
		}
		lock();
		(void)mix_of(m);
		unlock();
		/* the names, outside the lock: the process table and the objects have their own */
		for ( k = 0; k < (INT)m->napp; k++ ) {
			SDMixApp	*a = &m->app[k];

			if ( a->pid > 0 ) {
				(void)knl_prc_name(a->pid, a->name, SND_NAME_MAX);
			} else if ( !prog_none(a->prog) && prog_name(a->prog, a->name) ) {
				;
			} else {
				knl_strcpy((char *)a->name, "システムの音");
			}
		}
		reply(req, m, sizeof(SDMix));
		Kfree(m);
		return E_OK;
	}
	case DN_SDOUTSEL: {
		SDOutSel	s;

		knl_memset(&s, 0, sizeof(s));
		lock();
		id_copy(s.id, sel_id);
		if ( play_hw != NULL ) {
			id_copy(s.now, play_id);
		}
		unlock();
		reply(req, &s, sizeof(s));
		return E_OK;
	}
	case DN_SDSTAT: {
		SDStat	st;
		INT	i;

		lock();
		st = snd_stat;
		st.backend = ( play_hw != NULL ) ? (UW)play_hw->kind
			   : ( rec_hw != NULL ) ? (UW)rec_hw->kind : SND_BE_NONE;
		st.out_rate = ( play_hw != NULL ) ? mix_rate : 0;
		st.out_channels = ( play_hw != NULL ) ? (UW)play_channels : 0;
		st.in_rate = ( rec_hw != NULL ) ? rec_in_rate : 0;
		st.in_channels = ( rec_hw != NULL ) ? (UW)rec_in_channels : 0;
		st.channels = st.playing = 0;
		for ( i = 0; i < NCH; i++ ) {
			if ( ch[i].used ) {
				st.channels++;
				if ( playing(&ch[i]) ) {
					st.playing++;
				}
			}
		}
		unlock();
		reply(req, &st, sizeof(st));
		return E_OK;
	}
	default:
		if ( is_level((INT)req->start) ) {
			SDVol	v;
			CONST SNDHW *hw;

			knl_memset(&v, 0, sizeof(v));
			lock();
			hw = ( play_hw != NULL ) ? play_hw : hw_play();
			if ( hw == NULL ) {
				hw = hw_record();
			}
			er = ( hw != NULL ) ? hw->get_level((INT)req->start, &v) : E_NOSPT;
			unlock();
			if ( er < E_OK ) {
				return er;
			}
			reply(req, &v, sizeof(v));
			return E_OK;
		}
		return E_PAR;
	}
}

/* Playback data, into the free part of the channel's FIFO */
LOCAL INT write_pcm( CHAN *c, T_DEVREQ *req )
{
	INT	n = 0, want = (INT)req->size, t;

	if ( c->ctl == PCM_REC || c->ctl == PCM_REPPLAY ) {
		return 0;
	}
	if ( want <= 0 ) {
		return c->bufsz - 1 - (( c->fifo != NULL ) ? fifo_used(c) : 0);
	}
	if ( fifo_alloc(c) < E_OK ) {
		return 0;
	}
	if ( c->ctl == PCM_STOP ) {
		/* the first write of a stream; number 0 starts it at the top */
		if ( c->top && req->start == 0 ) {
			c->head = c->tail = 0;
		}
		c->top = FALSE;
	}
	t = c->tail;
	while ( n < want ) {
		INT	used = c->head - t, room, k;

		if ( used < 0 ) {
			used += c->bufsz;
		}
		room = c->bufsz - 1 - used;
		if ( room <= 0 ) {
			break;
		}
		k = ( c->head >= t ) ? c->bufsz - c->head : t - 1 - c->head;
		if ( c->head >= t && t == 0 ) {
			k--;			/* a byte stays free at the wrap */
		}
		if ( k > room ) {
			k = room;
		}
		if ( k > want - n ) {
			k = want - n;
		}
		if ( k <= 0 ) {
			break;
		}
		knl_memcpy(c->fifo + c->head, (UB *)req->buf + n, k);
		n += k;
		MB();
		c->head = ( c->head + k == c->bufsz ) ? 0 : c->head + k;
	}

	return n;
}

LOCAL ER proc_write( CHAN *c, T_DEVREQ *req )
{
	W	v = 0;
	ER	er;

	req->asize = req->size;
	if ( req->start != 0 && req->start != 1 && req->size >= (SZ)sizeof(W) ) {
		knl_memcpy(&v, req->buf, sizeof(W));
	}
	switch ( req->start ) {
	case 0:
	case 1:
		req->asize = write_pcm(c, req);
		return E_OK;
	case DN_SDPCMMODE: {
		SDPcmMode	m;

		if ( req->size < (SZ)sizeof(m) ) {
			return E_PAR;
		}
		if ( c->ctl != PCM_STOP ) {
			return E_BUSY;
		}
		knl_memcpy(&m, req->buf, sizeof(m));
		if ( m.sampling > PCMST48K || m.datafmt > PCMFmt16 ) {
			return E_NOSPT;
		}
		c->mode = m;
		return E_OK;
	}
	case DN_SDPCMCTL:
		if ( req->size < (SZ)sizeof(W) ) {
			return E_PAR;
		}
		if ( v == PCM_STOP ) {
			lock();
			chan_stop(c);
			unlock();
			return E_OK;
		}
		if ( c->ctl != PCM_STOP ) {
			return E_BUSY;
		}
		if ( v != PCM_REC && v != PCM_PLAY && v != PCM_REPPLAY ) {
			return E_PAR;
		}
		lock();
		er = ( v == PCM_REC ) ? start_rec(c) : start_play(c, v);
		unlock();
		return er;
	case DN_SDPCMENDCNT:
		if ( req->size < (SZ)sizeof(W) ) {
			return E_PAR;
		}
		c->endcnt = ( v <= 0 || v > PCM_COUNT_NONE ) ? PCM_COUNT_NONE : v;
		if ( c->ctl != PCM_STOP && limited(c) && c->count >= (UW)c->endcnt ) {
			lock();
			chan_stop(c);
			unlock();
		}
		return E_OK;
	case DN_SDPCMBUFSZ:
		if ( req->size < (SZ)sizeof(W) ) {
			return E_PAR;
		}
		if ( c->ctl != PCM_STOP ) {
			return E_BUSY;
		}
		if ( v <= 0 ) {
			return E_PAR;
		}
		if ( v > BUF_MAX ) {
			return E_NOMEM;
		}
		lock();
		if ( c->fifo != NULL ) {
			Kfree(c->fifo);
			c->fifo = NULL;
		}
		c->bufsz = v + 1;
		c->head = c->tail = 0;
		er = fifo_alloc(c);
		if ( er < E_OK ) {
			c->bufsz = BUF_DEFAULT + 1;
		}
		unlock();
		return er;
	case DN_SDSELIN:
	case DN_SDSELOUT:
		/* connected, and not to be changed */
		return ( req->size < (SZ)sizeof(SDSel) ) ? E_PAR : E_OK;
	case DN_SDMIXSET: {
		SDMixSet	m;

		if ( req->size < (SZ)sizeof(m) ) {
			return E_PAR;
		}
		knl_memcpy(&m, req->buf, sizeof(m));
		return knl_snd_mix_set(&m, TRUE);
	}
	case DN_SDOUTSEL: {
		UB	id[SND_ID_MAX];
		INT	i;

		/* the id alone, or a whole SDOutSel */
		for ( i = 0; i < (INT)req->size && i < SND_ID_MAX - 1 && ((UB *)req->buf)[i] != 0; i++ ) {
			id[i] = ((UB *)req->buf)[i];
		}
		id[i] = 0;
		return knl_snd_select(id);
	}
	default:
		if ( is_level((INT)req->start) ) {
			SDVol	s;
			CONST SNDHW *hw;

			if ( req->size < (SZ)sizeof(s) ) {
				return E_PAR;
			}
			knl_memcpy(&s, req->buf, sizeof(s));
			lock();
			hw = ( play_hw != NULL ) ? play_hw : hw_play();
			if ( hw == NULL ) {
				hw = hw_record();
			}
			er = ( hw != NULL ) ? hw->set_level((INT)req->start, &s) : E_NOSPT;
			unlock();
			return er;
		}
		return E_PAR;
	}
}

/* ---------------------------------------------------------------- driver */

LOCAL ER snd_open( ID devid, UINT omode, void *exinf )
{
	return E_OK;
}

/*
 * The last close of a unit. The device manager passes on only the first
 * open and the last close of a unit, not each process's, so a channel is
 * made at its first request and let go here, or when its process ends.
 */
LOCAL ER snd_close( ID devid, UINT option, void *exinf )
{
	INT	unit = (INT)(devid - snd_devid);
	INT	i;

	lock();
	for ( i = 0; i < NCH; i++ ) {
		if ( ch[i].used && ch[i].unit == unit ) {
			chan_free(&ch[i]);
		}
	}
	unlock();

	return E_OK;
}

LOCAL ER snd_exec( T_DEVREQ *req, TMO tmout, void *exinf )
{
	INT	unit = (INT)(req->devid - snd_devid);
	CHAN	*c;
	ID	key = requester();
	TS_UUID	prog;

	/* the program it is of, asked before the lock: the process table has its own */
	knl_memset(&prog, 0, sizeof(prog));
	if ( key > 0 ) {
		(void)knl_prc_uuid(key, NULL, &prog);
	}
	req->asize = 0;
	lock();
	c = chan_of(unit, key, (CONST UB *)&prog);
	unlock();
	if ( c == NULL ) {
		req->error = E_LIMIT;		/* too many programs */
		return E_OK;
	}
	req->error = ( req->cmd == TDC_READ ) ? proc_read(c, req) : proc_write(c, req);

	return E_OK;
}

LOCAL INT snd_wait( T_DEVREQ *req, INT nreq, TMO tmout, void *exinf )
{
	return ( nreq > 0 ) ? 0 : E_PAR;
}

LOCAL ER snd_abort( ID tskid, T_DEVREQ *req, INT nreq, void *exinf )
{
	return E_OK;
}

/* Suspend: every channel stops */
LOCAL INT snd_event( INT evttyp, void *evtinf, void *exinf )
{
	INT	i;

	if ( evttyp != TDV_SUSPEND ) {
		return E_OK;
	}
	lock();
	for ( i = 0; i < NCH; i++ ) {
		if ( ch[i].used ) {
			chan_stop(&ch[i]);
		}
	}
	unlock();

	return E_OK;
}

EXPORT ER knl_snd_init( void )
{
	T_DDEV	ddev;
	ID	id;

	if ( snd_ready ) {
		return E_OK;
	}
#ifdef RPI5
	knl_i2s_init();
	knl_hdmi_init();
#endif
	ddev.exinf   = NULL;
	ddev.drvatr  = 0;
	ddev.devatr  = TDK_SOUND;
	ddev.nsub    = SND_UNITS;
	ddev.blksz   = 1;
	ddev.openfn  = (FP)snd_open;
	ddev.closefn = (FP)snd_close;
	ddev.execfn  = (FP)snd_exec;
	ddev.waitfn  = (FP)snd_wait;
	ddev.abortfn = (FP)snd_abort;
	ddev.eventfn = (FP)snd_event;
	id = tk_def_dev((UB *)SND_DEVNM, &ddev, NULL);
	if ( id <= 0 ) {
		return id;
	}
	snd_devid = id;
	snd_ready = TRUE;

	return E_OK;
}
