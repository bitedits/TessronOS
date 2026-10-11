/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_tty.c
 *	Terminals (lib/libposix, design 5.8.4): termios, the foreground
 *	group, and the pseudo terminals of /dev, on the requests of
 *	sys/ioctl.h.
 */

#include "psx_int.h"
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int tcgetattr( int fd, struct termios *t )
{
	return ioctl(fd, TIOCGETA, t);
}

int tcsetattr( int fd, int how, const struct termios *t )
{
	unsigned long	req;

	switch ( how & ~TCSASOFT ) {
	  case TCSANOW:		req = TIOCSETA; break;
	  case TCSADRAIN:	req = TIOCSETAW; break;
	  case TCSAFLUSH:	req = TIOCSETAF; break;
	  default:
		errno = EINVAL;
		return -1;
	}
	return ioctl(fd, req, t);
}

speed_t cfgetispeed( const struct termios *t )		{ return (speed_t)t->c_ispeed; }
speed_t cfgetospeed( const struct termios *t )		{ return (speed_t)t->c_ospeed; }
int cfsetispeed( struct termios *t, speed_t s )		{ t->c_ispeed = (int)s; return 0; }
int cfsetospeed( struct termios *t, speed_t s )		{ t->c_ospeed = (int)s; return 0; }
int cfsetspeed( struct termios *t, speed_t s )		{ t->c_ispeed = t->c_ospeed = (int)s; return 0; }

void cfmakeraw( struct termios *t )
{
	t->c_iflag &= ~(tcflag_t)( IMAXBEL | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON );
	t->c_oflag &= ~(tcflag_t)OPOST;
	t->c_lflag &= ~(tcflag_t)( ECHO | ECHONL | ICANON | ISIG | IEXTEN );
	t->c_cflag &= ~(tcflag_t)( CSIZE | PARENB );
	t->c_cflag |= CS8 | CREAD;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}

int tcdrain( int fd )
{
	return ioctl(fd, TIOCDRAIN, NULL);
}

int tcflow( int fd, int action )
{
	(void)action;
	return isatty(fd) ? 0 : -1;
}

int tcflush( int fd, int which )
{
	int	v = ( which == TCIFLUSH ) ? FREAD : ( which == TCOFLUSH ) ? FWRITE : ( FREAD | FWRITE );

	return ioctl(fd, TIOCFLUSH, &v);
}

int tcsendbreak( int fd, int dur )
{
	(void)dur;
	return isatty(fd) ? 0 : -1;
}

pid_t tcgetpgrp( int fd )
{
	int	pg = 0;

	if ( ioctl(fd, TIOCGPGRP, &pg) < 0 ) return -1;
	return (pid_t)pg;
}

int tcsetpgrp( int fd, pid_t pg )
{
	int	v = (int)pg;

	return ioctl(fd, TIOCSPGRP, &v);
}

pid_t tcgetsid( int fd )
{
	int	s = 0;

	if ( ioctl(fd, TIOCGSID, &s) < 0 ) return -1;
	return (pid_t)s;
}

/* ---------------------------------------------------------------- names */

int ttyname_r( int fd, char *buf, size_t len )
{
	int	n = -1;

	if ( !isatty(fd) ) return ENOTTY;
	if ( ioctl(fd, TIOCGPTN, &n) < 0 ) return ENOTTY;
	if ( (size_t)snprintf(buf, len, "/dev/pts/%d", n) >= len ) return ERANGE;
	return 0;
}

char *ttyname( int fd )
{
	static char	buf[32];
	int		e = ttyname_r(fd, buf, sizeof(buf));

	if ( e != 0 ) {
		errno = e;
		return NULL;
	}
	return buf;
}

char *ctermid( char *s )
{
	static char	buf[16];

	if ( s == NULL ) s = buf;
	strcpy(s, "/dev/tty");
	return s;
}

int posix_openpt( int flags )
{
	return open("/dev/ptmx", flags);
}

int grantpt( int fd )
{
	return isatty(fd) ? 0 : -1;
}

int unlockpt( int fd )
{
	return isatty(fd) ? 0 : -1;
}

char *ptsname( int fd )
{
	static char	buf[32];
	int		n = -1;

	if ( ioctl(fd, TIOCGPTN, &n) < 0 ) return NULL;
	snprintf(buf, sizeof(buf), "/dev/pts/%d", n);
	return buf;
}

int openpty( int *amaster, int *aslave, char *name, const struct termios *t, const struct winsize *w )
{
	int	m, s;
	char	*n;

	m = posix_openpt(O_RDWR | O_NOCTTY);
	if ( m < 0 ) return -1;
	n = ptsname(m);
	s = ( n != NULL ) ? open(n, O_RDWR | O_NOCTTY) : -1;
	if ( s < 0 ) {
		(void)close(m);
		return -1;
	}
	if ( name != NULL ) strcpy(name, n);
	if ( t != NULL ) (void)tcsetattr(s, TCSANOW, t);
	if ( w != NULL ) (void)ioctl(s, TIOCSWINSZ, w);
	*amaster = m;
	*aslave = s;
	return 0;
}

/* The terminal as the session's, for a program that leads one (login_tty) */
int login_tty( int fd )
{
	(void)setsid();
	if ( ioctl(fd, TIOCSCTTY, NULL) < 0 ) return -1;
	(void)dup2(fd, 0);
	(void)dup2(fd, 1);
	(void)dup2(fd, 2);
	if ( fd > 2 ) (void)close(fd);
	return 0;
}
