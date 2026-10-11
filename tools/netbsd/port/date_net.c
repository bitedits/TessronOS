/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	date_net.c
 *	Setting the time of the machines of a network for date: there is
 *	no time daemon to ask, so the time is always set on this machine
 *	alone (a value other than 0 tells date to do so).
 */

#include <time.h>

int netsettime( time_t t )
{
	(void)t;
	return 1;
}
