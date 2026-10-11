/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	pipefs.c
 *	Pipes (design 5.8.2).
 *
 *	A pipe is a ring of PIPE_SIZE bytes in the kernel's memory with two
 *	open files of the file layer, the end that reads and the end that
 *	writes (knl_fs_pipe_as makes them). Each end may have many
 *	descriptors, in many processes; the layer closes an end when its
 *	last descriptor goes, and the pipe goes with its second end.
 *
 *	A read waits while the ring is empty and some end writes; with no
 *	end that writes it answers 0, the end of the file. A write waits for
 *	room, puts in what fits and waits again until all is in; with no end
 *	that reads it answers EX_PIPE and the writer is sent SIGPIPE. A write
 *	of at most PIPE_BUF bytes goes in whole or not at all, so that lines
 *	written by several processes do not mix. With FS_O_NONBLOCK neither
 *	waits (EX_AGAIN). Waiting is in slices of knl_fs_wait_slice, so a
 *	signal or the end of the process stops it (EX_INTR).
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <ts/fs.h>
#include <ts/psx.h>
#include <ts/time.h>
#include <ts/dt.h>

#define PIPE_SIZE	( 64 * 1024 )
#define PIPE_BUF	512
#define PIPE_SLICE	50		/* ms */

typedef struct {
	UB	*buf;
	UINT	head;			/* where the next byte read is */
	UINT	len;			/* bytes in the ring */
	INT	readers, writers;	/* ends not yet closed */
	UD	ino;
	TS_TIME	mtime;
} PIPE;

LOCAL ID	pipe_mtxid = 0;
LOCAL UD	pipe_seq = 0;

EXPORT ER knl_pipe_init( void )
{
	T_CMTX	c;

	if ( pipe_mtxid > 0 ) {
		return E_OK;
	}
	c.exinf = NULL;
	c.mtxatr = TA_TFIFO;
	pipe_mtxid = tk_cre_mtx(&c);
	return ( pipe_mtxid > 0 ) ? E_OK : (ER)pipe_mtxid;
}

LOCAL ER pipe_lock( void )
{
	if ( pipe_mtxid <= 0 ) {
		return E_OBJ;
	}
	tk_loc_mtx(pipe_mtxid, TMO_FEVR);
	return E_OK;
}

LOCAL void pipe_unlock( void )
{
	tk_unl_mtx(pipe_mtxid);
}

LOCAL BOOL is_reader( CONST T_FILE *f )
{
	return ( ( f->oflags & O_ACCMODE ) == O_RDONLY );
}

EXPORT ER knl_pipe_make( T_FILE *rd, T_FILE *wr )
{
	PIPE	*p;
	ER	er;

	p = (PIPE *)Kcalloc(1, sizeof(PIPE));
	if ( p == NULL ) {
		return EX_NOMEM;
	}
	p->buf = (UB *)Kmalloc(PIPE_SIZE);
	if ( p->buf == NULL ) {
		Kfree(p);
		return EX_NOMEM;
	}
	er = pipe_lock();
	if ( er < E_OK ) {
		Kfree(p->buf);
		Kfree(p);
		return EX_NOMEM;
	}
	p->readers = 1;
	p->writers = 1;
	p->ino = ++pipe_seq;
	(void)dt_gettime(&p->mtime);
	pipe_unlock();
	rd->obj = p;
	wr->obj = p;
	rd->ino = wr->ino = p->ino;
	return EX_OK;
}

LOCAL INT pipe_read( T_FILE *f, void *buf, SZ len )
{
	PIPE	*p = (PIPE *)f->obj;
	UB	*d = (UB *)buf;
	SZ	n, k;

	if ( p == NULL || !is_reader(f) ) {
		return EX_BADF;
	}
	for ( ;; ) {
		(void)pipe_lock();
		if ( p->len > 0 ) {
			n = ( len < (SZ)p->len ) ? len : (SZ)p->len;
			k = PIPE_SIZE - p->head;
			if ( k > n ) k = n;
			knl_memcpy(d, p->buf + p->head, (INT)k);
			if ( n > k ) {
				knl_memcpy(d + k, p->buf, (INT)( n - k ));
			}
			p->head = (UINT)( ( p->head + n ) % PIPE_SIZE );
			p->len -= (UINT)n;
			if ( p->len == 0 ) p->head = 0;
			pipe_unlock();
			knl_fs_wake();
			return (INT)n;
		}
		if ( p->writers == 0 ) {
			pipe_unlock();
			return 0;
		}
		pipe_unlock();
		if ( ( f->oflags & FS_O_NONBLOCK ) != 0 ) {
			return EX_AGAIN;
		}
		if ( !knl_fs_wait_slice(PIPE_SLICE) ) {
			return EX_INTR;
		}
	}
}

LOCAL INT pipe_write( T_FILE *f, CONST void *buf, SZ len )
{
	PIPE	*p = (PIPE *)f->obj;
	CONST UB *s = (CONST UB *)buf;
	SZ	done = 0, room, n, at, k;

	if ( p == NULL || is_reader(f) ) {
		return EX_BADF;
	}
	while ( done < len ) {
		(void)pipe_lock();
		if ( p->readers == 0 ) {
			pipe_unlock();
			(void)knl_psx_kill(ts_get_pid(), TS_SIGPIPE);
			return ( done > 0 ) ? (INT)done : EX_PIPE;
		}
		room = PIPE_SIZE - (SZ)p->len;
		n = len - done;
		if ( n > room ) n = room;
		if ( len - done <= PIPE_BUF && n < len - done ) {
			n = 0;				/* a short write goes in whole */
		}
		if ( n > 0 ) {
			at = (SZ)( ( p->head + p->len ) % PIPE_SIZE );
			k = PIPE_SIZE - at;
			if ( k > n ) k = n;
			knl_memcpy(p->buf + at, s + done, (INT)k);
			if ( n > k ) {
				knl_memcpy(p->buf, s + done + k, (INT)( n - k ));
			}
			p->len += (UINT)n;
			done += n;
			(void)dt_gettime(&p->mtime);
		}
		pipe_unlock();
		if ( n > 0 ) {
			knl_fs_wake();
			continue;
		}
		if ( ( f->oflags & FS_O_NONBLOCK ) != 0 ) {
			return ( done > 0 ) ? (INT)done : EX_AGAIN;
		}
		if ( !knl_fs_wait_slice(PIPE_SLICE) ) {
			return ( done > 0 ) ? (INT)done : EX_INTR;
		}
	}
	return (INT)done;
}

LOCAL ER pipe_close( T_FILE *f )
{
	PIPE	*p = (PIPE *)f->obj;
	BOOL	gone;

	if ( p == NULL ) {
		return EX_OK;
	}
	(void)pipe_lock();
	if ( is_reader(f) ) {
		p->readers--;
	} else {
		p->writers--;
	}
	gone = ( p->readers <= 0 && p->writers <= 0 );
	pipe_unlock();
	f->obj = NULL;
	if ( gone ) {
		Kfree(p->buf);
		Kfree(p);
	}
	knl_fs_wake();
	return EX_OK;
}

LOCAL UINT pipe_poll( T_FILE *f, UINT events )
{
	PIPE	*p = (PIPE *)f->obj;
	UINT	r = 0;

	(void)events;
	if ( p == NULL ) {
		return FS_POLL_NVAL;
	}
	(void)pipe_lock();
	if ( is_reader(f) ) {
		if ( p->len > 0 ) r |= FS_POLL_IN;
		if ( p->writers == 0 ) r |= FS_POLL_IN | FS_POLL_HUP;
	} else {
		if ( p->readers == 0 ) {
			r |= FS_POLL_ERR;
		} else if ( PIPE_SIZE - p->len >= PIPE_BUF ) {
			r |= FS_POLL_OUT;
		}
	}
	pipe_unlock();
	return r;
}

LOCAL ER pipe_fxstat( T_FILE *f, T_FSXSTAT *st )
{
	PIPE	*p = (PIPE *)f->obj;

	if ( p == NULL ) {
		return EX_BADF;
	}
	(void)pipe_lock();
	st->st.mode = FS_IFIFO | 0600;
	st->st.size = p->len;
	st->st.mtime = (UD)p->mtime;
	st->st.ino = p->ino;
	st->blksize = PIPE_BUF;
	pipe_unlock();
	return EX_OK;
}

LOCAL ER pipe_ioctl( T_FILE *f, UINT cmd, void *arg )
{
	PIPE	*p = (PIPE *)f->obj;

	if ( p == NULL ) {
		return EX_BADF;
	}
	if ( cmd == FS_IOC_NREAD ) {
		(void)pipe_lock();
		*(INT *)arg = is_reader(f) ? (INT)p->len : 0;
		pipe_unlock();
		return EX_OK;
	}
	return EX_NOTTY;
}

EXPORT CONST T_FIMP knl_fimp_pipe = {
	.name    = "pipe",
	.close   = pipe_close,
	.read    = pipe_read,
	.write   = pipe_write,
	.fxstat  = pipe_fxstat,
	.ioctl   = pipe_ioctl,
	.poll    = pipe_poll,
};
