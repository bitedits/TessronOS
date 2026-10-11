/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	rtm_dec.c
 *	A file of sound made into what the mixer plays: 48 kHz, stereo,
 *	16 bit (design 15.6)
 *
 *	WAVE (integer 8, 16, 24 and 32 bit, 32 bit float, the extensible
 *	header) is read as it is; MP3 is decoded with minimp3. The kind is
 *	taken from what the file begins with, then from the format the
 *	record named, then from the name's extension. Any other rate is
 *	brought to 48 kHz by drawing a straight line between neighbouring
 *	samples; one channel is put on both sides, more than two give the
 *	first two.
 */

#include "rtm.h"
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static UINT rd16( CONST UB *p ) { return (UINT)p[0] | ( (UINT)p[1] << 8 ); }
static UINT rd32( CONST UB *p ) { return rd16(p) | ( rd16(p + 2) << 16 ); }

/* Interleaved stereo at 'rate' made into FS; frees nothing */
static H *resample( CONST H *in, INT frames, INT rate, INT *p_out )
{
	INT	n, i;
	H	*out;
	D	step, pos = 0;

	if ( rate == FS || rate <= 0 ) {
		out = (H *)malloc(sizeof(H) * 2 * (SZ)( frames > 0 ? frames : 1 ));
		if ( out == NULL ) return NULL;
		memcpy(out, in, sizeof(H) * 2 * (SZ)frames);
		*p_out = frames;
		return out;
	}
	n = (INT)( (D)frames * FS / rate );
	out = (H *)malloc(sizeof(H) * 2 * (SZ)( n > 0 ? n : 1 ));
	if ( out == NULL ) return NULL;
	step = ( (D)rate << 16 ) / FS;
	for ( i = 0; i < n; i++, pos += step ) {
		INT	k = (INT)( pos >> 16 ), f = (INT)( pos & 0xFFFF ), c;

		for ( c = 0; c < 2; c++ ) {
			INT	a = in[2 * k + c], b = ( k + 1 < frames ) ? in[2 * ( k + 1 ) + c] : a;

			out[2 * i + c] = (H)( a + ( ( b - a ) * f >> 16 ) );
		}
	}
	*p_out = n;
	return out;
}

/* Samples of 'bits' (8 unsigned, 16, 24, 32 signed, or 32 float) into stereo 16 bit */
static H *to_stereo( CONST UB *d, INT frames, INT channels, INT bits, BOOL fl )
{
	H	*o = (H *)malloc(sizeof(H) * 2 * (SZ)( frames > 0 ? frames : 1 ));
	INT	bps = bits / 8, i, c;

	if ( o == NULL ) return NULL;
	for ( i = 0; i < frames; i++ ) {
		for ( c = 0; c < 2; c++ ) {
			INT		ch = ( channels == 1 ) ? 0 : c;
			CONST UB	*p = d + ( (SZ)i * channels + ch ) * bps;
			INT		v;

			if ( fl && bits == 32 ) {
				float	f;

				memcpy(&f, p, 4);
				if ( f > 1.0f ) f = 1.0f;
				if ( f < -1.0f ) f = -1.0f;
				v = (INT)( f * 32767.0f );
			} else if ( bits == 8 ) {
				v = ( (INT)p[0] - 128 ) << 8;
			} else if ( bits == 16 ) {
				v = (H)rd16(p);
			} else if ( bits == 24 ) {
				v = (INT)( ( (UINT)p[0] << 8 ) | ( (UINT)p[1] << 16 ) | ( (UINT)p[2] << 24 ) ) >> 16;
			} else {
				v = (INT)rd32(p) >> 16;
			}
			o[2 * i + c] = (H)v;
		}
	}
	return o;
}

EXPORT ER dec_raw( CONST UB *data, SZ size, INT rate, INT channels, INT bits, H **p_pcm, INT *p_frames )
{
	H	*st, *out;
	INT	frames, n;

	if ( channels <= 0 ) channels = 2;
	if ( bits != 8 && bits != 16 && bits != 24 && bits != 32 ) bits = 16;
	if ( rate <= 0 ) rate = 44100;
	frames = (INT)( size / ( (SZ)channels * ( bits / 8 ) ) );
	if ( frames <= 0 ) return E_OBJ;
	st = to_stereo(data, frames, channels, bits, FALSE);
	if ( st == NULL ) return E_NOMEM;
	out = resample(st, frames, rate, &n);
	free(st);
	if ( out == NULL ) return E_NOMEM;
	*p_pcm = out;
	*p_frames = n;
	return E_OK;
}

static ER dec_wav( CONST UB *d, SZ size, H **p_pcm, INT *p_frames )
{
	SZ	at = 12;
	INT	fmt = 0, ch = 0, rate = 0, bits = 0;
	CONST UB *body = NULL;
	SZ	blen = 0;
	H	*st, *out;
	INT	frames, n;

	while ( at + 8 <= size ) {
		UINT	len = rd32(d + at + 4);

		if ( memcmp(d + at, "fmt ", 4) == 0 && len >= 16 && at + 8 + len <= size ) {
			fmt = (INT)rd16(d + at + 8);
			ch = (INT)rd16(d + at + 10);
			rate = (INT)rd32(d + at + 12);
			bits = (INT)rd16(d + at + 22);
			if ( fmt == 0xFFFE && len >= 26 ) fmt = (INT)rd16(d + at + 32);	/* extensible: its sub format */
		} else if ( memcmp(d + at, "data", 4) == 0 ) {
			body = d + at + 8;
			blen = ( at + 8 + len <= size ) ? len : size - at - 8;
			break;
		}
		at += 8 + len + ( len & 1 );
	}
	if ( body == NULL || ch <= 0 || rate <= 0 ) return E_OBJ;
	if ( fmt != 1 && fmt != 3 ) return E_NOSPT;		/* compressed WAVE */
	if ( bits != 8 && bits != 16 && bits != 24 && bits != 32 ) return E_NOSPT;
	frames = (INT)( blen / ( (SZ)ch * ( bits / 8 ) ) );
	if ( frames <= 0 ) return E_OBJ;
	st = to_stereo(body, frames, ch, bits, (BOOL)( fmt == 3 ));
	if ( st == NULL ) return E_NOMEM;
	out = resample(st, frames, rate, &n);
	free(st);
	if ( out == NULL ) return E_NOMEM;
	*p_pcm = out;
	*p_frames = n;
	return E_OK;
}

static ER dec_mp3( CONST UB *d, SZ size, H **p_pcm, INT *p_frames )
{
	mp3dec_t		*dec = (mp3dec_t *)malloc(sizeof(mp3dec_t));
	mp3dec_frame_info_t	info;
	short			pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	H			*all = NULL, *out;
	SZ			cap = 0, nf = 0, at = 0;
	INT			rate = 0, n;

	if ( dec == NULL ) return E_NOMEM;
	mp3dec_init(dec);
	/* past an ID3v2 tag */
	if ( size > 10 && d[0] == 'I' && d[1] == 'D' && d[2] == '3' ) {
		at = 10 + ( ( (SZ)( d[6] & 0x7F ) << 21 ) | ( (SZ)( d[7] & 0x7F ) << 14 )
			  | ( (SZ)( d[8] & 0x7F ) << 7 ) | (SZ)( d[9] & 0x7F ) );
	}
	while ( at < size ) {
		INT	s = mp3dec_decode_frame(dec, d + at, (int)( size - at ), pcm, &info);
		INT	i;

		if ( info.frame_bytes <= 0 ) break;
		at += (SZ)info.frame_bytes;
		if ( s <= 0 ) continue;
		if ( rate == 0 ) rate = info.hz;
		if ( nf + (SZ)s > cap ) {
			H	*nb;

			cap = ( cap == 0 ) ? 1152 * 64 : cap * 2;
			nb = (H *)realloc(all, sizeof(H) * 2 * cap);
			if ( nb == NULL ) {
				free(all);
				free(dec);
				return E_NOMEM;
			}
			all = nb;
		}
		for ( i = 0; i < s; i++ ) {
			all[2 * ( nf + i )] = pcm[i * info.channels];
			all[2 * ( nf + i ) + 1] = pcm[i * info.channels + ( info.channels > 1 ? 1 : 0 )];
		}
		nf += (SZ)s;
	}
	free(dec);
	if ( nf == 0 ) {
		free(all);
		return E_OBJ;
	}
	out = resample(all, (INT)nf, rate, &n);
	free(all);
	if ( out == NULL ) return E_NOMEM;
	*p_pcm = out;
	*p_frames = n;
	return E_OK;
}

static BOOL ends_with( CONST UB *s, CONST char *ext )
{
	SZ	n = ( s != NULL ) ? strlen((CONST char *)s) : 0, e = strlen(ext), i;

	if ( n < e ) return FALSE;
	for ( i = 0; i < e; i++ ) {
		UB	c = s[n - e + i];

		if ( c >= 'A' && c <= 'Z' ) c = (UB)( c - 'A' + 'a' );
		if ( c != (UB)ext[i] ) return FALSE;
	}
	return TRUE;
}

EXPORT ER dec_any( CONST UB *data, SZ size, CONST UB *name, CONST UB *format, H **p_pcm, INT *p_frames )
{
	if ( size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0 ) {
		return dec_wav(data, size, p_pcm, p_frames);
	}
	if ( ( size >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3' )
	  || ( size >= 2 && data[0] == 0xFF && ( data[1] & 0xE0 ) == 0xE0 )
	  || ( format != NULL && strcmp((CONST char *)format, "mp3") == 0 ) || ends_with(name, ".mp3") ) {
		return dec_mp3(data, size, p_pcm, p_frames);
	}
	if ( ( format != NULL && strcmp((CONST char *)format, "pcm") == 0 ) || ends_with(name, ".pcm")
	  || ends_with(name, ".raw") ) {
		return dec_raw(data, size, 44100, 2, 16, p_pcm, p_frames);
	}
	return E_NOSPT;		/* ogg, flac, aac, opus: nothing here decodes them */
}
