/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	rtm_synth.c
 *	The instruments notes and MIDI messages are played on (design 15.6.3)
 *
 *	There is no MIDI device to send a note to, so every note is made
 *	here, in software, and mixed with the rest. Each family of
 *	instruments of General MIDI is one way of making a sound: a few
 *	harmonics of a sine with a level that falls (piano, bass, bell), or
 *	is held (organ, strings, brass, reed, flute), a plucked string
 *	(guitar: a delay line that is averaged as it goes round), and for
 *	the drums of channel 10 a sweep of a sine (bass drum, toms) or noise
 *	through a filter (snare, cymbals). A stream named "synthX:" is made
 *	of the wave and the envelope its parameters say.
 */

#include "rtm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TAB	4096
static float	sine[TAB];

EXPORT void syn_init( void )
{
	INT	i;

	for ( i = 0; i < TAB; i++ ) sine[i] = (float)sin(2.0 * 3.14159265358979 * i / TAB);
}

static inline float osc( UINT ph )
{
	return sine[ph >> 20];
}

/* The partials of a family: multiple of the frequency and level */
typedef struct {
	float	mul[4], amp[4];
	float	attack, tau, release;	/* seconds; tau 0: held */
} FAM;

static CONST FAM fam[11] = {
	/* piano */	{ { 1, 2, 3, 4 }, { 1.0f, 0.45f, 0.25f, 0.12f }, 0.002f, 1.6f, 0.12f },
	/* organ */	{ { 1, 2, 3, 4 }, { 1.0f, 0.6f, 0.4f, 0.2f }, 0.01f, 0, 0.05f },
	/* guitar */	{ { 1, 0, 0, 0 }, { 1.0f, 0, 0, 0 }, 0.001f, 2.5f, 0.1f },
	/* bass */	{ { 1, 2, 3, 0 }, { 1.0f, 0.35f, 0.1f, 0 }, 0.004f, 1.2f, 0.08f },
	/* strings */	{ { 1, 2, 3, 4 }, { 1.0f, 0.5f, 0.33f, 0.25f }, 0.08f, 0, 0.25f },
	/* brass */	{ { 1, 2, 3, 4 }, { 1.0f, 0.7f, 0.5f, 0.3f }, 0.04f, 0, 0.1f },
	/* reed */	{ { 1, 3, 5, 7 }, { 1.0f, 0.4f, 0.2f, 0.1f }, 0.03f, 0, 0.08f },
	/* bell */	{ { 1, 2.76f, 5.4f, 8.93f }, { 1.0f, 0.5f, 0.3f, 0.15f }, 0.001f, 1.8f, 0.4f },
	/* synth */	{ { 1, 0, 0, 0 }, { 1.0f, 0, 0, 0 }, 0.01f, 0, 0.1f },
	/* drums */	{ { 1, 0, 0, 0 }, { 1.0f, 0, 0, 0 }, 0.0f, 0.3f, 0.05f },
	/* flute */	{ { 1, 2, 3, 0 }, { 1.0f, 0.1f, 0.03f, 0 }, 0.05f, 0, 0.08f },
};

EXPORT void syn_instr( INSTR *ins, INT family )
{
	memset(ins, 0, sizeof(*ins));
	ins->family = family;
	ins->wave = W_SINE;
	ins->attack = fam[family].attack;
	ins->decay = fam[family].tau;
	ins->sustain = ( fam[family].tau == 0 ) ? 1.0f : 0.0f;
	ins->release = fam[family].release;
	ins->gain = ( family == IN_DRUMS ) ? 0.6f : 0.35f;
}

/* General MIDI's program number into a family */
EXPORT void syn_gm( INSTR *ins, INT p )
{
	INT	f = IN_PIANO;

	if ( p < 8 ) f = IN_PIANO;
	else if ( p < 16 ) f = IN_BELL;
	else if ( p < 24 ) f = IN_ORGAN;
	else if ( p < 32 ) f = IN_GUITAR;
	else if ( p < 40 ) f = IN_BASS;
	else if ( p < 56 ) f = IN_STRINGS;
	else if ( p < 64 ) f = IN_BRASS;
	else if ( p < 72 ) f = IN_REED;
	else if ( p < 80 ) f = IN_FLUTE;
	else if ( p < 104 ) f = IN_SYNTH;
	else if ( p < 112 ) f = IN_GUITAR;
	else if ( p < 120 ) f = IN_BELL;
	syn_instr(ins, f);
	if ( f == IN_SYNTH ) ins->wave = ( p < 88 ) ? W_SAW : W_TRIANGLE;
}

static BOOL has( CONST UB *s, CONST char *w )
{
	SZ	n = strlen(w), i, k;

	if ( s == NULL ) return FALSE;
	for ( i = 0; s[i] != 0; i++ ) {
		for ( k = 0; k < n; k++ ) {
			UB	a = s[i + k], b = (UB)w[k];

			if ( a >= 'A' && a <= 'Z' ) a = (UB)( a - 'A' + 'a' );
			if ( a != b ) break;
		}
		if ( k == n ) return TRUE;
	}
	return FALSE;
}

/* The value of "name=" in a list of parameters, or -1 */
static float param( CONST UB *s, CONST char *name )
{
	CONST UB	*p;
	SZ		n = strlen(name);

	for ( p = s; p != NULL && *p != 0; p++ ) {
		if ( ( p == s || p[-1] == ',' || p[-1] == ' ' || p[-1] == ':' )
		  && strncmp((CONST char *)p, name, n) == 0 && p[n] == '=' ) {
			return (float)atof((CONST char *)p + n + 1);
		}
	}
	return -1.0f;
}

EXPORT BOOL syn_device( INSTR *ins, CONST UB *device, CONST UB *params, INT *p_chan )
{
	CONST UB	*c;
	INT		fam_no = IN_PIANO;

	/* "ch10" in the parameters: the channel the stream's notes go on */
	for ( c = params; c != NULL && *c != 0; c++ ) {
		if ( ( c[0] == 'c' || c[0] == 'C' ) && ( c[1] == 'h' || c[1] == 'H' ) && c[2] >= '0' && c[2] <= '9' ) {
			*p_chan = atoi((CONST char *)c + 2);
			break;
		}
	}
	if ( device == NULL ) {
		syn_instr(ins, ( *p_chan == 10 ) ? IN_DRUMS : IN_PIANO);
		return FALSE;
	}
	if ( has(device, "drum") || has(device, "percussion") || *p_chan == 10 ) fam_no = IN_DRUMS;
	else if ( has(device, "bass") ) fam_no = IN_BASS;
	else if ( has(device, "guitar") || has(device, "harp") ) fam_no = IN_GUITAR;
	else if ( has(device, "organ") ) fam_no = IN_ORGAN;
	else if ( has(device, "string") || has(device, "violin") || has(device, "viola") || has(device, "cello") ) {
		fam_no = IN_STRINGS;
	} else if ( has(device, "brass") || has(device, "trumpet") || has(device, "horn")
		 || has(device, "trombone") ) {
		fam_no = IN_BRASS;
	} else if ( has(device, "sax") || has(device, "clarinet") || has(device, "oboe") || has(device, "reed") ) {
		fam_no = IN_REED;
	} else if ( has(device, "flute") || has(device, "pipe") ) {
		fam_no = IN_FLUTE;
	} else if ( has(device, "bell") || has(device, "glocken") || has(device, "celesta")
		 || has(device, "vibra") || has(device, "marimba") ) {
		fam_no = IN_BELL;
	} else if ( has(device, "synth") || has(device, "lead") || has(device, "pad") ) {
		fam_no = IN_SYNTH;
	}
	syn_instr(ins, fam_no);
	if ( fam_no == IN_SYNTH ) {
		/* synthX:wave=square,attack=10,decay=200,sustain=0.5,release=100,volume=0.8 */
		CONST UB	*all = device;
		float		v;

		if ( has(all, "wave=sine") || has(params, "wave=sine") ) ins->wave = W_SINE;
		else if ( has(all, "wave=square") || has(params, "wave=square") ) ins->wave = W_SQUARE;
		else if ( has(all, "wave=saw") || has(params, "wave=saw") ) ins->wave = W_SAW;
		else if ( has(all, "wave=tri") || has(params, "wave=tri") ) ins->wave = W_TRIANGLE;
		else if ( has(all, "wave=noise") || has(params, "wave=noise") ) ins->wave = W_NOISE;
		else ins->wave = W_SAW;
		if ( ( v = param(all, "attack") ) >= 0 || ( v = param(params, "attack") ) >= 0 ) ins->attack = v / 1000.0f;
		if ( ( v = param(all, "decay") ) >= 0 || ( v = param(params, "decay") ) >= 0 ) ins->decay = v / 1000.0f;
		if ( ( v = param(all, "sustain") ) >= 0 || ( v = param(params, "sustain") ) >= 0 ) ins->sustain = v;
		if ( ( v = param(all, "release") ) >= 0 || ( v = param(params, "release") ) >= 0 ) ins->release = v / 1000.0f;
		if ( ( v = param(all, "volume") ) >= 0 || ( v = param(params, "volume") ) >= 0 ) ins->gain = 0.35f * v;
	}
	return TRUE;
}

/* ---------------------------------------------------------------- voices */

static float mtof( INT key )
{
	return 440.0f * powf(2.0f, ( key - 69 ) / 12.0f);
}

EXPORT void syn_start( VOICE *v )
{
	INT	k;

	v->freq = mtof(v->key);
	v->env = 0;
	v->age = 0;
	v->rel = -1;
	for ( k = 0; k < 4; k++ ) v->ph[k] = 0;
	v->noise = 0x12345678U ^ (UINT)( v->key * 2654435761U );
	v->lp = v->hp = 0;
	v->amp = powf(v->vel / 127.0f, 1.5f) * v->ins.gain;
	if ( v->ins.family == IN_GUITAR ) {
		/* a plucked string: noise round a line as long as one period */
		v->ks_len = (INT)( FS / v->freq );
		if ( v->ks_len < 2 ) v->ks_len = 2;
		v->ks = (float *)malloc(sizeof(float) * (SZ)v->ks_len);
		if ( v->ks != NULL ) {
			for ( k = 0; k < v->ks_len; k++ ) {
				v->noise = v->noise * 1664525U + 1013904223U;
				v->ks[k] = ( (INT)( v->noise >> 16 ) - 32768 ) / 32768.0f;
			}
		}
		v->ks_at = 0;
	}
}

static float noise( VOICE *v )
{
	v->noise = v->noise * 1664525U + 1013904223U;
	return ( (INT)( v->noise >> 16 ) - 32768 ) / 32768.0f;
}

/* One sample of a drum of the General MIDI kit */
static float drum( VOICE *v )
{
	float	t = v->age / (float)FS, s = 0, e;
	INT	k = v->key;

	if ( k == 35 || k == 36 ) {			/* bass drum: a falling sine */
		float	f = 45.0f + 75.0f * expf(-t * 30.0f);

		v->ph[0] += (UINT)( f / FS * 4294967296.0f );
		s = osc(v->ph[0]) * expf(-t * 9.0f);
	} else if ( k == 38 || k == 40 || k == 39 ) {	/* snare, clap: noise and a tone */
		float	n = noise(v);

		v->lp += 0.35f * ( n - v->lp );
		v->ph[0] += (UINT)( 185.0f / FS * 4294967296.0f );
		e = expf(-t * ( k == 39 ? 22.0f : 16.0f ));
		s = v->lp * e + osc(v->ph[0]) * expf(-t * 30.0f) * 0.5f;
	} else if ( k == 42 || k == 44 || k == 46 || k == 49 || k == 51 || k == 52 || k == 55
		 || k == 57 || k == 59 ) {		/* cymbals: high noise */
		float	n = noise(v);
		float	d = ( k == 42 || k == 44 ) ? 45.0f : ( k == 46 ) ? 9.0f : ( k == 51 || k == 59 ) ? 3.5f : 2.0f;

		v->hp = 0.6f * ( v->hp + n - v->lp );	/* the first difference: what is high */
		v->lp = n;
		s = v->hp * expf(-t * d) * 0.6f;
	} else if ( k >= 41 && k <= 50 ) {		/* toms: a sine that falls a little */
		float	f0 = 80.0f + ( k - 41 ) * 22.0f;
		float	f = f0 * ( 1.0f + 0.5f * expf(-t * 20.0f) );

		v->ph[0] += (UINT)( f / FS * 4294967296.0f );
		s = osc(v->ph[0]) * expf(-t * 7.0f);
	} else if ( k == 56 ) {				/* cowbell */
		v->ph[0] += (UINT)( 540.0f / FS * 4294967296.0f );
		v->ph[1] += (UINT)( 800.0f / FS * 4294967296.0f );
		s = ( ( v->ph[0] >> 31 ) ? 0.5f : -0.5f ) + ( ( v->ph[1] >> 31 ) ? 0.5f : -0.5f );
		s *= expf(-t * 10.0f) * 0.5f;
	} else {					/* anything else: a short click of noise */
		s = noise(v) * expf(-t * 40.0f);
	}
	return s;
}

/* One sample of the wave of a synth */
static float wave( VOICE *v, UINT inc )
{
	UINT	ph = v->ph[0];

	v->ph[0] += inc;
	switch ( v->ins.wave ) {
	case W_SQUARE:	return ( ph >> 31 ) ? 0.6f : -0.6f;
	case W_SAW:	return ( (INT)( ph >> 16 ) - 32768 ) / 49152.0f;
	case W_TRIANGLE: {
		INT	x = (INT)( ph >> 16 );

		return ( x < 32768 ? x : 65535 - x ) / 16384.0f - 1.0f;
	}
	case W_NOISE:	return noise(v) * 0.6f;
	default:	return osc(ph);
	}
}

EXPORT BOOL syn_render( VOICE *v, float *l, float *r, INT n, D at )
{
	CONST FAM	*f = &fam[v->ins.family];
	float		att = v->ins.attack, rel = v->ins.release;
	float		fr = v->freq * ( v->bend > 0 ? v->bend : 1.0f );
	float		gl = ( v->pan <= 0 ) ? 1.0f : 1.0f - v->pan;
	float		gr = ( v->pan >= 0 ) ? 1.0f : 1.0f + v->pan;
	float		dk = ( v->ins.decay > 0 ) ? expf(-1.0f / ( v->ins.decay * FS )) : 1.0f;
	float		rk = expf(-1.0f / ( ( rel > 0.005f ? rel : 0.005f ) * FS / 5.0f ));
	UINT		inc[4];
	INT		i, k;

	for ( k = 0; k < 4; k++ ) inc[k] = (UINT)( fr * f->mul[k] / FS * 4294967296.0f );
	for ( i = 0; i < n; i++, at++ ) {
		float	s = 0, e;

		if ( v->rel < 0 && v->off_at >= 0 && at >= v->off_at ) v->rel = 0;
		/* the level: up through the attack, then falling or held, then let go */
		if ( v->ins.family == IN_DRUMS ) {
			e = 1.0f;
		} else if ( v->age < att * FS ) {
			e = v->age / ( att * FS );
			v->env = e;
		} else {
			if ( v->ins.decay > 0 ) {
				v->env *= dk;
				if ( v->ins.sustain > 0 && v->env < v->ins.sustain ) v->env = v->ins.sustain;
			} else {
				v->env = 1.0f;
			}
			e = v->env;
		}
		if ( v->rel >= 0 && v->ins.family != IN_DRUMS ) {
			/* let go: from where it was, down by the release */
			if ( v->rel == 0 ) {
				v->rlev = e;
				v->rgain = 1.0f;
			} else {
				v->rgain *= rk;
			}
			e = v->rlev * v->rgain;
			v->rel++;
			if ( v->rgain < 0.0005f && v->rel > 8 ) return FALSE;
		}
		switch ( v->ins.family ) {
		case IN_DRUMS:
			s = drum(v);
			if ( v->age > FS * 3 ) return FALSE;
			break;
		case IN_GUITAR:
			if ( v->ks != NULL ) {
				INT	a = v->ks_at, b = ( a + 1 ) % v->ks_len;

				s = v->ks[a];
				v->ks[a] = 0.4985f * ( v->ks[a] + v->ks[b] );
				v->ks_at = b;
			}
			break;
		case IN_SYNTH:
			s = wave(v, inc[0]);
			break;
		case IN_FLUTE:
			s = osc(v->ph[0]) + 0.1f * osc(v->ph[1]) + 0.03f * noise(v);
			v->ph[0] += inc[0];
			v->ph[1] += inc[1];
			break;
		default:
			for ( k = 0; k < 4; k++ ) {
				if ( f->amp[k] != 0 ) s += f->amp[k] * osc(v->ph[k]);
				v->ph[k] += inc[k];
			}
			s *= 0.55f;
			break;
		}
		s *= e * v->amp;
		l[i] += s * gl;
		r[i] += s * gr;
		v->age++;
		if ( v->ins.decay > 0 && v->ins.sustain == 0 && v->ins.family != IN_DRUMS && v->age > att * FS
		  && v->env < 0.0003f ) {
			return FALSE;
		}
	}
	return TRUE;
}
