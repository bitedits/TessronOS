/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/event.h
 *	kqueue's names (design 5.8.3). No event can be watched: kevent says
 *	ENOSYS, and a command that can look again now and then does so.
 */

#ifndef _SYS_EVENT_H_
#define _SYS_EVENT_H_

#include <sys/types.h>
#include <stdint.h>
#include <time.h>

#define EVFILT_READ		0
#define EVFILT_WRITE		1
#define EVFILT_AIO		2
#define EVFILT_VNODE		3
#define EVFILT_PROC		4
#define EVFILT_SIGNAL		5
#define EVFILT_TIMER		6

#define EV_ADD			0x0001
#define EV_DELETE		0x0002
#define EV_ENABLE		0x0004
#define EV_DISABLE		0x0008
#define EV_ONESHOT		0x0010
#define EV_CLEAR		0x0020
#define EV_EOF			0x8000
#define EV_ERROR		0x4000

#define NOTE_DELETE		0x0001
#define NOTE_WRITE		0x0002
#define NOTE_EXTEND		0x0004
#define NOTE_ATTRIB		0x0008
#define NOTE_LINK		0x0010
#define NOTE_RENAME		0x0020
#define NOTE_REVOKE		0x0040
#define NOTE_EXIT		0x80000000

struct kevent {
	uintptr_t	ident;
	uint32_t	filter;
	uint32_t	flags;
	uint32_t	fflags;
	int64_t		data;
	void		*udata;
};

#define EV_SET(kevp, a, b, c, d, e, f) do {	\
	(kevp)->ident = (uintptr_t)(a);		\
	(kevp)->filter = (b);			\
	(kevp)->flags = (c);			\
	(kevp)->fflags = (d);			\
	(kevp)->data = (e);			\
	(kevp)->udata = (void *)(f);		\
} while ( 0 )

#ifdef __cplusplus
extern "C" {
#endif

int	kqueue( void );
int	kqueue1( int );
int	kevent( int, const struct kevent *, size_t, struct kevent *, size_t, const struct timespec * );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_EVENT_H_ */
