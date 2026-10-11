/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_start.c
 *	From _start to main for a POSIX program (lib/libposix, design 5.8.3)
 *
 *	The start-up argument (ts_get_arg) says what the program is given.
 *	Three forms come:
 *
 *	- the spawn's (posix_spawn, psx_spawn.c): PSX_ARG_MAGIC, then the
 *	  numbers of PSXARG, the POSIX number each descriptor handed over
 *	  is to have, and the strings: argv, the environment, the place it
 *	  starts in. The descriptors themselves come with ts_get_inh, in the
 *	  same order.
 *	- a line of UTF-8, the words a space apart, as the console and the
 *	  programs of the system start a command with ob_cre_obj: the words
 *	  after the program's name.
 *	- none, or a UUID of 16 bytes (the object the desktop opened the
 *	  program with): no words.
 *
 *	Without the spawn's form the program has the system's console as its
 *	standard output and error, nothing to read on its standard input,
 *	the place /home and the environment of a session (PATH=/bin and so
 *	on). The main function runs on a stack of PSX_STACK bytes the program
 *	makes for itself, with a page that is never made below it.
 */

#include "psx_int.h"
#include <ts/umem.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <paths.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>

#define PSX_STACK	( 1024 * 1024 )
#define PAGE		4096UL
#define PSX_ARG_MAGIC	"\0TSPX1\0"		/* 8 bytes with the nought after it */

/* The head of the spawn's form, after its 8 bytes of magic */
typedef struct {
	UW	size;			/* bytes of the whole */
	UW	argc;
	UW	envc;
	UW	nfd;			/* descriptors handed over */
	UW	umask;
	UW	flags;
	UW	fdmap;			/* offset of nfd UW: the POSIX number of each (PSX_FDM_*) */
	UW	strs;			/* offset of the strings */
} PSXARG;

#define PSX_FDM_NUM	0x0000ffff
#define PSX_FDM_CONOUT	0x00010000	/* not a descriptor: the console, written */
#define PSX_FDM_CONIN	0x00020000	/* not a descriptor: nothing to read */

#define PSX_AF_TCSETPGRP 0x0001		/* make its group the terminal's foreground first */
#define PSX_AF_SETCTTY	0x0002		/* a session leader: its standard input its terminal */

static int	psx_take_tty, psx_set_ctty;

extern int	main( int, char **, char ** );
extern void	__psx_on_stack( void *sp, void (*fn)( void * ), void *arg );
extern const char __psx_default_name[] __attribute__((weak));

PSXFD		__psx_fd[PSX_FD_MAX];
char		**environ;
unsigned int	__psx_umask = 022;
const char	*__psx_progname = "program";
extern const char *__progname __attribute__((alias("__psx_progname")));	/* BSD's name of it */

static char	*psx_argv0[2];
static char	**psx_argv;
static int	psx_argc;

static const char *const psx_env0[] = {
	"PATH=/bin",
	"HOME=" _PATH_HOME,
	"LANG=ja_JP.UTF-8",
	"LC_CTYPE=ja_JP.UTF-8",
	"TERM=vt100",
	"SHELL=/bin/tsh",
	"PAGER=less",
	"EDITOR=vi",
	"TMPDIR=/tmp",
	NULL
};

/* A copy of the default environment, in memory the program may change */
static char **env_default( void )
{
	int	n, i;
	char	**e;

	for ( n = 0; psx_env0[n] != NULL; n++ ) ;
	e = (char **)malloc(sizeof(char *) * (size_t)( n + 1 ));
	if ( e == NULL ) return NULL;
	for ( i = 0; i < n; i++ ) e[i] = strdup(psx_env0[i]);
	e[n] = NULL;
	return e;
}

/* The words of a line, a space apart, as argv after argv[0] */
static void args_line( const char *s, size_t n )
{
	char	*copy;
	size_t	i;
	int	words = 0, k = 0;

	copy = (char *)malloc(n + 1);
	if ( copy == NULL ) return;
	memcpy(copy, s, n);
	copy[n] = 0;
	for ( i = 0; i < n; i++ ) {
		if ( copy[i] != ' ' && ( i == 0 || copy[i - 1] == ' ' || copy[i - 1] == 0 ) ) words++;
	}
	psx_argv = (char **)malloc(sizeof(char *) * (size_t)( words + 2 ));
	if ( psx_argv == NULL ) return;
	psx_argv[k++] = (char *)__psx_progname;
	for ( i = 0; i < n; i++ ) {
		if ( copy[i] == ' ' ) {
			copy[i] = 0;
		} else if ( i == 0 || copy[i - 1] == 0 ) {
			psx_argv[k++] = &copy[i];
		}
	}
	psx_argv[k] = NULL;
	psx_argc = k;
}

/* The spawn's form: argv, the environment, the place, the descriptors */
static int args_spawn( const char *buf, size_t n )
{
	const PSXARG	*h = (const PSXARG *)( buf + 8 );
	const UW	*map;
	const char	*s, *end = buf + n;
	int		*kfd = NULL, got, i;

	if ( n < 8 + sizeof(PSXARG) || h->size > n || h->strs > n || h->fdmap > n
	  || h->nfd > TS_SPN_FD_MAX || h->fdmap + h->nfd * sizeof(UW) > n ) {
		return -1;
	}
	map = (const UW *)( buf + h->fdmap );
	s = buf + h->strs;
	psx_argc = (int)h->argc;
	psx_argv = (char **)malloc(sizeof(char *) * ( h->argc + 1 ));
	environ = (char **)malloc(sizeof(char *) * ( h->envc + 1 ));
	if ( psx_argv == NULL || environ == NULL ) return -1;
	for ( i = 0; i < (int)h->argc && s < end; i++ ) {
		psx_argv[i] = strdup(s);
		s += strlen(s) + 1;
	}
	psx_argv[i] = NULL;
	psx_argc = i;
	for ( i = 0; i < (int)h->envc && s < end; i++ ) {
		environ[i] = strdup(s);
		s += strlen(s) + 1;
	}
	environ[i] = NULL;
	if ( s < end && s[0] == '/' && strlen(s) < PATH_MAX ) {
		strcpy(__psx_cwd, s);
	}
	__psx_umask = h->umask & 0777;
	psx_take_tty = ( h->flags & PSX_AF_TCSETPGRP ) != 0;
	psx_set_ctty = ( h->flags & PSX_AF_SETCTTY ) != 0;

	if ( h->nfd > 0 ) {
		kfd = (int *)malloc(sizeof(int) * h->nfd);
		got = ( kfd != NULL ) ? ts_get_inh(kfd, (INT)h->nfd) : 0;
	} else {
		got = 0;
	}
	for ( i = 0; i < (int)h->nfd; i++ ) {
		unsigned int	num = map[i] & PSX_FDM_NUM;

		if ( num >= PSX_FD_MAX ) continue;
		if ( ( map[i] & PSX_FDM_CONOUT ) != 0 ) {
			__psx_fd[num].kind = PK_CONOUT;
		} else if ( ( map[i] & PSX_FDM_CONIN ) != 0 ) {
			__psx_fd[num].kind = PK_CONIN;
		} else if ( i < got ) {
			__psx_fd[num].kind = PK_FS;
			__psx_fd[num].kfd = kfd[i];
		}
	}
	if ( kfd != NULL ) free(kfd);
	return 0;
}

/*
 * A job of a shell in the foreground: its group made the terminal's
 * foreground before the program reads it, which a background group could
 * not do (SIGTTIN). Held back meanwhile is SIGTTOU, which asking it from
 * the background would send.
 */
static void take_tty( void )
{
	sigset_t	m, old;
	int		fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);

	if ( fd < 0 ) return;
	sigemptyset(&m);
	sigaddset(&m, SIGTTOU);
	(void)sigprocmask(SIG_BLOCK, &m, &old);
	(void)tcsetpgrp(fd, getpgrp());
	(void)sigprocmask(SIG_SETMASK, &old, NULL);
	(void)close(fd);
}

static void psx_main( void *arg )
{
	(void)arg;
	exit(main(psx_argc, psx_argv, environ));
}

void __psx_start( void )
{
	static char	small[512];
	char		*buf = small;
	SZ		n;
	void		*map = NULL;
	char		*hi;

	if ( __psx_default_name != NULL ) {
		__psx_progname = __psx_default_name;
	}
	strcpy(__psx_cwd, _PATH_HOME);

	n = ts_get_arg(NULL, 0);
	if ( n > (SZ)sizeof(small) ) {
		buf = (char *)malloc((size_t)n);
		if ( buf == NULL ) n = 0;
	}
	if ( n > 0 ) {
		n = ts_get_arg(buf, n);
	}
	if ( n >= 8 + (SZ)sizeof(PSXARG) && memcmp(buf, PSX_ARG_MAGIC, 8) == 0
	  && args_spawn(buf, (size_t)n) == 0 ) {
		/* the spawn's form */
	} else {
		__psx_fd[0].kind = PK_CONIN;
		__psx_fd[1].kind = PK_CONOUT;
		__psx_fd[2].kind = PK_CONOUT;
		environ = env_default();
		if ( n > 0 && n != 16 && buf[0] != 0 ) {
			size_t	len = strnlen(buf, (size_t)n);

			args_line(buf, len);
		}
	}
	if ( psx_argv == NULL ) {
		psx_argv0[0] = (char *)__psx_progname;
		psx_argv0[1] = NULL;
		psx_argv = psx_argv0;
		psx_argc = 1;
	}
	if ( psx_argc > 0 && psx_argv[0] != NULL ) {
		const char *b = strrchr(psx_argv[0], '/');

		__psx_progname = ( b != NULL ) ? b + 1 : psx_argv[0];
	}
	if ( buf != small ) free(buf);

	(void)setlocale(LC_ALL, "");
	if ( getenv("LANG") == NULL && getenv("LC_ALL") == NULL && getenv("LC_CTYPE") == NULL ) {
		(void)setlocale(LC_CTYPE, "C.UTF-8");
	}
	__psx_sig_init();
	__psx_tz_init();
	if ( psx_set_ctty ) (void)ioctl(0, TIOCSCTTY, 0);	/* the program a terminal is opened with */
	if ( psx_take_tty ) take_tty();

	/* a stack of its own, a page never made below it */
	if ( ts_map_mem(&map, (SZ)( PAGE + PSX_STACK ), 0, TS_MEM_NONE) >= E_OK
	  && ts_ctl_mem((char *)map + PAGE, (SZ)PSX_STACK, TS_MEM_RW) >= E_OK ) {
		hi = (char *)map + PAGE + PSX_STACK;
		__psx_on_stack(hi, psx_main, NULL);
	}
	psx_main(NULL);
}
