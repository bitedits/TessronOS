/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_lh5e.c
 *	The LH5 encoder (design 11.20)
 *
 *	The output is what lh5_decode reads: a stream of bits, the most
 *	significant first, cut into blocks. A block is its count of codes
 *	(16 bits) and three tables -- the code lengths of the code lengths,
 *	the code lengths of the 510 codes for a byte or a length, and those
 *	of the 14 codes for a distance's bit count -- and then its codes.
 *	A table of one code is written as a count of 0 and that code, and
 *	its code then takes no bits.
 *
 *	A code below 256 is a byte. Above, it is a length of 3 to 256
 *	(253 + length) and a distance of 1 to 8192 follows: d = distance - 1
 *	as the number of its bits (0 for d 0), then the bits below the top
 *	one.
 *
 *	Matches are found through chains of the earlier places whose next
 *	three bytes hash alike, within the 8 KB window. When the place one
 *	byte on has a longer match, the byte goes out as it is and that
 *	match is taken instead.
 *
 *	Code lengths are made by Huffman's method over a heap. A tree
 *	deeper than 16 is made again from frequencies halved (none below 1)
 *	until it is not, which flattens it. Codes are given in order of
 *	length, and of the code within a length, as the decoder's tables
 *	expect.
 */

#include <ts/bpk.h>

#define CBIT		9
#define TBIT		5
#define PBIT		4
#define MAXBITS		16
#define LAZY_UNDER	32		/* a match this long is not put off for the next one */

/* ---------------------------------------------------------------- bits */

LOCAL void put_byte( BPKLH5E *z, UINT b )
{
	if ( z->outpos >= z->outmax ) {
		z->full = TRUE;
		return;
	}
	z->out[z->outpos++] = (UB)b;
}

/* The low n bits of v (n up to 16), the highest first */
LOCAL void putbits( BPKLH5E *z, INT n, UINT v )
{
	INT	i;

	for ( i = n - 1; i >= 0; i-- ) {
		z->bitbuf = ( z->bitbuf << 1 ) | ( ( v >> i ) & 1 );
		if ( ++z->bitcount == 8 ) {
			put_byte(z, z->bitbuf & 0xFF);
			z->bitbuf = 0;
			z->bitcount = 0;
		}
	}
}

LOCAL void flush_bits( BPKLH5E *z )
{
	if ( z->bitcount > 0 ) {
		put_byte(z, ( z->bitbuf << ( 8 - z->bitcount ) ) & 0xFF);
		z->bitbuf = 0;
		z->bitcount = 0;
	}
}

/* ---------------------------------------------------------------- Huffman */

/* Whether node a weighs less than node b; the older first among equals */
LOCAL BOOL lighter( BPKLH5E *z, INT a, INT b )
{
	return (BOOL)( z->hw[a] < z->hw[b] || ( z->hw[a] == z->hw[b] && a < b ) );
}

LOCAL void heap_down( BPKLH5E *z, INT n, INT i )
{
	INT	k = z->heap[i], c;

	while ( ( c = 2 * i + 1 ) < n ) {
		if ( c + 1 < n && lighter(z, z->heap[c + 1], z->heap[c]) ) c++;
		if ( !lighter(z, z->heap[c], k) ) break;
		z->heap[i] = z->heap[c];
		i = c;
	}
	z->heap[i] = k;
}

LOCAL INT heap_pop( BPKLH5E *z, INT *n )
{
	INT	top = z->heap[0];

	z->heap[0] = z->heap[--*n];
	if ( *n > 0 ) heap_down(z, *n, 0);
	return top;
}

LOCAL void heap_push( BPKLH5E *z, INT *n, INT k )
{
	INT	i = (*n)++, p;

	while ( i > 0 && lighter(z, k, z->heap[p = ( i - 1 ) / 2]) ) {
		z->heap[i] = z->heap[p];
		i = p;
	}
	z->heap[i] = k;
}

/*
 * The code lengths of nchar codes of the frequencies given, none longer
 * than MAXBITS. Answers how many codes are used; with one, its length
 * is 0 (the table of one code), with none all are 0.
 */
LOCAL INT make_len( BPKLH5E *z, const UINT *freq, INT nchar, UB *len )
{
	INT	i, used = 0, n, node, deep;

	for ( i = 0; i < nchar; i++ ) {
		len[i] = 0;
		z->hw[i] = freq[i];
		if ( freq[i] != 0 ) used++;
	}
	if ( used < 2 ) return used;
	for ( ;; ) {
		n = 0;
		for ( i = 0; i < nchar; i++ ) {
			if ( z->hw[i] != 0 ) heap_push(z, &n, i);
		}
		node = nchar;
		while ( n > 1 ) {
			INT	a = heap_pop(z, &n), b = heap_pop(z, &n);

			z->hw[node] = z->hw[a] + z->hw[b];
			z->hpar[a] = z->hpar[b] = node;
			heap_push(z, &n, node);
			node++;
		}
		/* the root is the last made; every other node is deeper than its parent */
		z->hdep[node - 1] = 0;
		deep = 0;
		for ( i = node - 2; i >= nchar; i-- ) z->hdep[i] = (UB)( z->hdep[z->hpar[i]] + 1 );
		for ( i = 0; i < nchar; i++ ) {
			if ( z->hw[i] == 0 ) continue;
			len[i] = (UB)( z->hdep[z->hpar[i]] + 1 );
			if ( len[i] > deep ) deep = len[i];
		}
		if ( deep <= MAXBITS ) return used;
		for ( i = 0; i < nchar; i++ ) {
			if ( z->hw[i] != 0 ) z->hw[i] = ( z->hw[i] >> 1 ) | 1;
		}
	}
}

/* The codes of the lengths: by length, and in each length in the order of the codes */
LOCAL void make_code( INT nchar, const UB *len, UH *code )
{
	UINT	count[MAXBITS + 2], next[MAXBITS + 2], c = 0;
	INT	i;

	for ( i = 0; i <= MAXBITS + 1; i++ ) count[i] = 0;
	for ( i = 0; i < nchar; i++ ) count[len[i]]++;
	count[0] = 0;
	for ( i = 1; i <= MAXBITS; i++ ) {
		next[i] = c;
		c = ( c + count[i] ) << 1;
	}
	for ( i = 0; i < nchar; i++ ) {
		if ( len[i] != 0 ) code[i] = (UH)next[len[i]]++;
	}
}

/* ---------------------------------------------------------------- the tables */

/*
 * A small table (the code lengths of the code lengths, the distances):
 * the count of lengths in nbit bits, then each length in 3 bits, or for
 * 7 and more as 111, a 1 for each above 7 and a 0. After the entry
 * before `special`, two bits say how many zeroes follow (up to 3).
 */
LOCAL void write_pt( BPKLH5E *z, const UB *len, INT nn, INT nbit, INT special, INT used, UINT single )
{
	INT	n = nn, i, k;

	if ( used < 2 ) {
		putbits(z, nbit, 0);
		putbits(z, nbit, single);
		return;
	}
	while ( n > 0 && len[n - 1] == 0 ) n--;
	putbits(z, nbit, (UINT)n);
	i = 0;
	while ( i < n ) {
		k = len[i++];
		if ( k <= 6 ) putbits(z, 3, (UINT)k);
		else putbits(z, k - 3, ( 1U << ( k - 3 ) ) - 2);
		if ( i == special ) {
			INT	zeroes = 0;

			while ( zeroes < 3 && i + zeroes < n && len[i + zeroes] == 0 ) zeroes++;
			putbits(z, 2, (UINT)zeroes);
			i += zeroes;
		}
	}
}

/* The one code a table of one code has; 0 for an empty table */
LOCAL UINT only_code( const UINT *freq, INT nchar )
{
	INT	i;

	for ( i = 0; i < nchar; i++ ) {
		if ( freq[i] != 0 ) return (UINT)i;
	}
	return 0;
}

/*
 * The code lengths of the 510 codes, each through the table of the code
 * lengths: a length L as L + 2, a run of zeroes as 0 (one), 1 and 4
 * bits (3 to 18), or 2 and 9 bits (20 and more); 19 is one and 18.
 * With count TRUE nothing is written: the codes are only counted.
 */
LOCAL void put_t( BPKLH5E *z, BOOL count, INT used, UINT c )
{
	if ( count ) z->t_freq[c]++;
	else if ( used >= 2 ) putbits(z, z->t_len[c], z->t_code[c]);
}

LOCAL void walk_c_len( BPKLH5E *z, BOOL count, INT tused )
{
	INT	n = LH5_NC, i = 0, k;

	while ( n > 0 && z->c_len[n - 1] == 0 ) n--;
	if ( !count ) putbits(z, CBIT, (UINT)n);
	while ( i < n ) {
		k = z->c_len[i++];
		if ( k != 0 ) {
			put_t(z, count, tused, (UINT)k + 2);
			continue;
		}
		for ( k = 1; i < n && z->c_len[i] == 0; i++ ) k++;
		if ( k <= 2 ) {
			while ( k-- > 0 ) put_t(z, count, tused, 0);
		} else if ( k <= 18 ) {
			put_t(z, count, tused, 1);
			if ( !count ) putbits(z, 4, (UINT)( k - 3 ));
		} else if ( k == 19 ) {
			put_t(z, count, tused, 0);
			put_t(z, count, tused, 1);
			if ( !count ) putbits(z, 4, 15);
		} else {
			put_t(z, count, tused, 2);
			if ( !count ) putbits(z, CBIT, (UINT)( k - 20 ));
		}
	}
}

/* The bit count of a distance less one: 0 for 0 */
LOCAL UINT dist_bits( UINT d )
{
	UINT	j = 0;

	while ( d != 0 ) {
		j++;
		d >>= 1;
	}
	return j;
}

/* ---------------------------------------------------------------- a block */

LOCAL void flush_block( BPKLH5E *z )
{
	INT	i, cused, pused, tused = 0;
	UINT	csingle, psingle, tsingle = 0;

	if ( z->ncode == 0 ) return;
	for ( i = 0; i < LH5_NC; i++ ) z->c_freq[i] = 0;
	for ( i = 0; i < LH5_NP; i++ ) z->p_freq[i] = 0;
	for ( i = 0; i < LH5_NT; i++ ) z->t_freq[i] = 0;
	for ( i = 0; i < z->ncode; i++ ) {
		z->c_freq[z->code[i]]++;
		if ( z->code[i] >= 256 ) z->p_freq[dist_bits(z->dist[i])]++;
	}
	cused = make_len(z, z->c_freq, LH5_NC, z->c_len);
	csingle = only_code(z->c_freq, LH5_NC);
	make_code(LH5_NC, z->c_len, z->c_code);
	pused = make_len(z, z->p_freq, LH5_NP, z->p_len);
	psingle = only_code(z->p_freq, LH5_NP);
	make_code(LH5_NP, z->p_len, z->p_code);
	if ( cused >= 2 ) {
		walk_c_len(z, TRUE, 0);
		tused = make_len(z, z->t_freq, LH5_NT, z->t_len);
		tsingle = only_code(z->t_freq, LH5_NT);
		make_code(LH5_NT, z->t_len, z->t_code);
	}

	putbits(z, 16, (UINT)z->ncode);
	if ( cused >= 2 ) {
		write_pt(z, z->t_len, LH5_NT, TBIT, 3, tused, tsingle);
		walk_c_len(z, FALSE, tused);
	} else {
		/* one code: its table needs no table of code lengths */
		putbits(z, TBIT, 0);
		putbits(z, TBIT, 0);
		putbits(z, CBIT, 0);
		putbits(z, CBIT, csingle);
	}
	write_pt(z, z->p_len, LH5_NP, PBIT, -1, pused, psingle);

	for ( i = 0; i < z->ncode && !z->full; i++ ) {
		UINT	c = z->code[i];

		if ( cused >= 2 ) putbits(z, z->c_len[c], z->c_code[c]);
		if ( c >= 256 ) {
			UINT	d = z->dist[i], j = dist_bits(d);

			if ( pused >= 2 ) putbits(z, z->p_len[j], z->p_code[j]);
			if ( j > 1 ) putbits(z, (INT)j - 1, d & ( ( 1U << ( j - 1 ) ) - 1 ));
		}
	}
	z->ncode = 0;
}

LOCAL void emit( BPKLH5E *z, UINT code, UINT dist )
{
	z->code[z->ncode] = (UH)code;
	z->dist[z->ncode] = (UH)dist;
	if ( ++z->ncode == LH5E_BLOCK ) flush_block(z);
}

/* ---------------------------------------------------------------- matches */

LOCAL UINT hash3( const UB *p )
{
	return ( ( (UINT)p[0] << 10 ) ^ ( (UINT)p[1] << 5 ) ^ p[2] ) & ( LH5E_HASH - 1 );
}

/* Place pos put at the head of its chain */
LOCAL void insert( BPKLH5E *z, const UB *in, UINT inlen, UINT pos )
{
	UINT	h;

	if ( pos + 3 > inlen ) return;
	h = hash3(in + pos);
	z->prev[pos & ( LH5E_WIN - 1 )] = z->head[h];
	z->head[h] = (INT)pos + 1;
}

/* The longest match for pos within the window: its length (0 none) and distance */
LOCAL UINT longest( BPKLH5E *z, const UB *in, UINT inlen, UINT pos, UINT *p_dist )
{
	UINT	max = inlen - pos, best = 0;
	INT	cand, tries = LH5E_CHAIN;

	if ( max > LH5_MAXMATCH ) max = LH5_MAXMATCH;
	if ( max < LH5_THRESHOLD ) return 0;
	cand = z->head[hash3(in + pos)] - 1;
	while ( cand >= 0 && (UINT)cand < pos && pos - (UINT)cand <= LH5E_WIN && tries-- > 0 ) {
		const UB	*a = in + cand, *b = in + pos;
		UINT		n;
		INT		next;

		if ( a[best] == b[best] && a[0] == b[0] ) {
			for ( n = 0; n < max && a[n] == b[n]; n++ ) ;
			if ( n > best ) {
				best = n;
				*p_dist = pos - (UINT)cand;
				if ( n == max ) break;
			}
		}
		next = z->prev[cand & ( LH5E_WIN - 1 )] - 1;
		if ( next >= cand ) break;		/* that place's slot is a later one's now */
		cand = next;
	}
	return ( best >= LH5_THRESHOLD ) ? best : 0;
}

EXPORT INT lh5_encode( BPKLH5E *z, const UB *in, UINT inlen, UB *out, UINT outmax )
{
	UINT	pos = 0, len, dist = 0, i;

	z->out = out;
	z->outmax = outmax;
	z->outpos = 0;
	z->full = FALSE;
	z->bitbuf = 0;
	z->bitcount = 0;
	z->ncode = 0;
	for ( i = 0; i < LH5E_HASH; i++ ) z->head[i] = 0;
	for ( i = 0; i < LH5E_WIN; i++ ) z->prev[i] = 0;

	while ( pos < inlen && !z->full ) {
		len = longest(z, in, inlen, pos, &dist);
		if ( len >= LH5_THRESHOLD && len < LAZY_UNDER && pos + 1 < inlen ) {
			UINT	d2 = 0, l2;

			insert(z, in, inlen, pos);
			l2 = longest(z, in, inlen, pos + 1, &d2);
			if ( l2 > len ) {
				emit(z, in[pos], 0);
				pos++;
				len = l2;
				dist = d2;
			} else {
				/* pos is in its chain already */
				emit(z, 253 + len, dist - 1);
				for ( i = 1; i < len; i++ ) insert(z, in, inlen, pos + i);
				pos += len;
				continue;
			}
		}
		if ( len >= LH5_THRESHOLD ) {
			emit(z, 253 + len, dist - 1);
			for ( i = 0; i < len; i++ ) insert(z, in, inlen, pos + i);
			pos += len;
		} else {
			emit(z, in[pos], 0);
			insert(z, in, inlen, pos);
			pos++;
		}
	}
	flush_block(z);
	flush_bits(z);
	return z->full ? -1 : (INT)z->outpos;
}
