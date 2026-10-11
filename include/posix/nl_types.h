/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	nl_types.h
 *	Catalogues of messages (design 5.8.3). There are none: a catalogue
 *	does not open, and catgets gives back the message the program has
 *	of its own.
 */

#ifndef _NL_TYPES_H_
#define _NL_TYPES_H_

#define NL_SETD		1
#define NL_CAT_LOCALE	1

typedef struct __psx_nl_cat	*nl_catd;
typedef long			nl_item;

#ifdef __cplusplus
extern "C" {
#endif

nl_catd	catopen( const char *, int );
char	*catgets( nl_catd, int, int, const char * );
int	catclose( nl_catd );

#ifdef __cplusplus
}
#endif

#endif /* _NL_TYPES_H_ */
