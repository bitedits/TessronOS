/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	poll.h
 *	Waiting for several descriptors (design 5.8.2): the file layer's
 *	fs_poll, whose event bits are these.
 */

#ifndef _POLL_H_
#define _POLL_H_
#define _SYS_POLL_H_

typedef unsigned int	nfds_t;

struct pollfd {
	int	fd;
	short	events;
	short	revents;
};

#define POLLIN		0x0001
#define POLLPRI		0x0002
#define POLLOUT		0x0004
#define POLLRDNORM	0x0040
#define POLLWRNORM	POLLOUT
#define POLLRDBAND	0x0080
#define POLLWRBAND	0x0100
#define POLLERR		0x0008
#define POLLHUP		0x0010
#define POLLNVAL	0x0020

#define INFTIM		-1

#ifdef __cplusplus
extern "C" {
#endif

int	poll( struct pollfd *, nfds_t, int );

#ifdef __cplusplus
}
#endif

#endif /* _POLL_H_ */
