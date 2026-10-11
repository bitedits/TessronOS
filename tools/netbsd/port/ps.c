/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ps.c
 *	The processes of the system (design 5.8.7): their list is asked of
 *	the kernel (ts_lst_prc) and each of them with ts_ref_psx, as there
 *	is no kernel memory to read. The options are those of BSD's ps
 *	that make sense here, given with or without a "-":
 *
 *	(none)	the processes of the calling terminal
 *	a	the processes of every terminal
 *	x	also those with no terminal
 *	e, A	every process (a and x)
 *	l, f	the parent, group, session and memory too
 *	u	the user and memory too
 *	p pid	only these processes (a list split by commas)
 *	t tty	only the processes of a terminal (ptsN, pts/N or N)
 *	w	the names are not cut
 */

#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/psx.h>
#include <ts/proc.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PS_MAX		1024		/* processes listed at most */

static int	all_tty, no_tty, longf, userf, wide;
static int	only_tty = -2;			/* -2: any terminal, -1: none */
static ID	only_pid[64];
static int	n_only_pid;

static void usage( void )
{
	fprintf(stderr, "usage: ps [-aAelfuwx] [-p pid[,pid...]] [-t tty]\n");
	exit(1);
}

static int tty_number( const char *s )
{
	if ( strncmp(s, "/dev/", 5) == 0 ) s += 5;
	if ( strncmp(s, "pts/", 4) == 0 ) s += 4;
	else if ( strncmp(s, "pts", 3) == 0 ) s += 3;
	if ( !isdigit((unsigned char)*s) ) {
		errx(1, "%s: not a terminal", s);
	}
	return atoi(s);
}

static void pid_list( const char *s )
{
	char	*e;
	long	v;

	while ( *s != 0 ) {
		v = strtol(s, &e, 10);
		if ( e == s || v <= 0 ) errx(1, "%s: not a process number", s);
		if ( n_only_pid < (int)( sizeof(only_pid) / sizeof(only_pid[0]) ) ) {
			only_pid[n_only_pid++] = (ID)v;
		}
		s = ( *e == ',' || *e == ' ' ) ? e + 1 : e;
	}
}

/* The options, BSD's way (one word of letters, "-" or not) */
static void options( int argc, char **argv )
{
	int	i;
	const char *a;

	for ( i = 1; i < argc; i++ ) {
		a = argv[i];
		if ( *a == '-' ) a++;
		for ( ; *a != 0; a++ ) {
			switch ( *a ) {
			  case 'a':	all_tty = 1; break;
			  case 'x':	no_tty = 1; break;
			  case 'e':
			  case 'A':	all_tty = no_tty = 1; break;
			  case 'l':
			  case 'f':	longf = 1; break;
			  case 'u':	userf = 1; break;
			  case 'w':	wide = 1; break;
			  case 'p':
				if ( a[1] != 0 ) pid_list(a + 1);
				else if ( ++i < argc ) pid_list(argv[i]);
				else usage();
				a += strlen(a) - 1;
				break;
			  case 't':
				if ( a[1] != 0 ) only_tty = tty_number(a + 1);
				else if ( ++i < argc ) only_tty = tty_number(argv[i]);
				else usage();
				a += strlen(a) - 1;
				break;
			  default:
				usage();
			}
		}
	}
}

static int wanted( const T_PSXREF *r, int mytty )
{
	int	i;

	if ( n_only_pid > 0 ) {
		for ( i = 0; i < n_only_pid; i++ ) {
			if ( only_pid[i] == r->pid ) return 1;
		}
		return 0;
	}
	if ( only_tty != -2 ) return r->tty == only_tty;
	if ( r->tty < 0 ) return no_tty;
	if ( all_tty ) return 1;
	if ( mytty >= 0 ) return r->tty == mytty;
	return 1;			/* started with no terminal: its own are all */
}

static void state_of( const T_PSXREF *r, char *s )
{
	*s++ = ( r->state == PS_ZOMBIE ) ? 'Z' : ( r->state == PS_EXITING ) ? 'E'
	     : r->stopped ? 'T' : ( r->pid == ts_get_pid() ) ? 'R' : 'S';
	if ( r->sid == r->pid ) *s++ = 's';
	*s = 0;
}

int main( int argc, char **argv )
{
	static ID	pids[PS_MAX];
	T_PSXREF	r, me;
	char		tty[16], st[8], tm[16], *user;
	struct passwd	*pw;
	int		n, i, mytty = -1, cols = 0;
	unsigned long	sec;

	options(argc, argv);
	if ( ts_ref_psx(ts_get_pid(), &me) >= 0 ) mytty = me.tty;
	n = ts_lst_prc(pids, PS_MAX);
	if ( n < 0 ) errx(1, "the processes cannot be listed (%d)", n);
	if ( !wide && isatty(STDOUT_FILENO) ) cols = 80;

	pw = getpwuid(getuid());
	user = ( pw != NULL ) ? pw->pw_name : "?";
	if ( userf ) printf("%-8s ", "USER");
	printf("%5s ", "PID");
	if ( longf ) printf("%5s %5s %5s ", "PPID", "PGID", "SID");
	if ( longf || userf ) printf("%7s ", "RSS");
	printf("%-6s %-4s %8s %s\n", "TTY", "STAT", "TIME", "COMMAND");

	for ( i = 0; i < n; i++ ) {
		if ( ts_ref_psx(pids[i], &r) < 0 ) continue;	/* ended meanwhile */
		if ( !wanted(&r, mytty) ) continue;
		if ( r.tty >= 0 ) snprintf(tty, sizeof(tty), "pts/%d", (int)r.tty);
		else strcpy(tty, "?");
		state_of(&r, st);
		sec = (unsigned long)( r.cpu_ms / 1000 );
		snprintf(tm, sizeof(tm), "%lu:%02lu.%02lu", sec / 60, sec % 60,
			 (unsigned long)( r.cpu_ms % 1000 ) / 10);
		r.name[sizeof(r.name) - 1] = 0;

		if ( userf ) printf("%-8s ", user);
		printf("%5d ", (int)r.pid);
		if ( longf ) printf("%5d %5d %5d ", (int)r.ppid, (int)r.pgid, (int)r.sid);
		if ( longf || userf ) printf("%7lu ", (unsigned long)( r.memsz / 1024 ));
		if ( cols > 0 ) {
			int used = 5 + 1 + 6 + 1 + 4 + 1 + 8 + 1 + ( userf ? 9 : 0 )
				 + ( longf ? 18 : 0 ) + ( ( longf || userf ) ? 8 : 0 );

			printf("%-6s %-4s %8s %.*s\n", tty, st, tm,
			       ( cols - used > 8 ) ? cols - used : 8, (char *)r.name);
		} else {
			printf("%-6s %-4s %8s %s\n", tty, st, tm, (char *)r.name);
		}
	}
	return 0;
}
