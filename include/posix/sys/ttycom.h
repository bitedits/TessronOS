/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/ttycom.h
 *	The requests ioctl takes of a terminal and of a pipe (design 5.8.4).
 *	lib/libposix turns each into the file layer's (include/ts/fs.h
 *	FS_IOC_*); the numbers here are only names between the two.
 */

#ifndef _SYS_TTYCOM_H_
#define _SYS_TTYCOM_H_

struct winsize {
	unsigned short	ws_row;
	unsigned short	ws_col;
	unsigned short	ws_xpixel;
	unsigned short	ws_ypixel;
};

#define TIOCGETA	0x7401		/* struct termios * */
#define TIOCSETA	0x7402
#define TIOCSETAW	0x7403
#define TIOCSETAF	0x7404
#define TIOCGWINSZ	0x7405		/* struct winsize * */
#define TIOCSWINSZ	0x7406
#define TIOCGPGRP	0x7407		/* int * */
#define TIOCSPGRP	0x7408
#define TIOCSCTTY	0x7409
#define TIOCNOTTY	0x740a
#define TIOCGPTN	0x740b		/* int *: the number of /dev/pts/N */
#define TIOCFLUSH	0x740c		/* int *: FREAD, FWRITE */
#define TIOCDRAIN	0x740d
#define TIOCGSID	0x740e
#define TIOCPTSNAME	0x740f
#define TIOCEXCL	0x7410
#define TIOCNXCL	0x7411
#define TIOCSTI		0x7412
#define TIOCOUTQ	0x7413
#define TIOCSTOP	0x7414
#define TIOCSTART	0x7415
#define FIONREAD	0x741b		/* int * */
#define FIONBIO		0x741c		/* int * */
#define FIOCLEX		0x741d
#define FIONCLEX	0x741e
#define FIOASYNC	0x741f

#endif /* _SYS_TTYCOM_H_ */
