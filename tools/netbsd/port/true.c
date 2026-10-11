/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	true.c
 *	true and false: nothing is done, and the program ends with 0 or,
 *	when it is named false, with 1.
 */

#include <stdlib.h>
#include <string.h>

extern const char *getprogname( void );

int main( void )
{
	return ( strcmp(getprogname(), "false") == 0 ) ? EXIT_FAILURE : EXIT_SUCCESS;
}
