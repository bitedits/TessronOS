/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	setjmp.h
 *	newlib's, with the jumps that keep the signal mask (design 5.8.5):
 *	sigsetjmp keeps the mask when asked, siglongjmp puts it back.
 */

#ifndef __POSIX_SETJMP_H__
#define __POSIX_SETJMP_H__

#include_next <setjmp.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	jmp_buf		__jb;
	int		__saved;
	sigset_t	__mask;
} sigjmp_buf[1];

/* setjmp by a name of its own, so that a program's macro named setjmp does not come in */
extern int __psx_setjmp( jmp_buf ) __asm__("setjmp") __attribute__((__returns_twice__));
extern void __psx_sigsave( sigjmp_buf, int );

#define sigsetjmp(env, save)	\
	( __psx_sigsave((env), (save)), __psx_setjmp((env)[0].__jb) )

void	siglongjmp( sigjmp_buf, int ) __attribute__((__noreturn__));

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_SETJMP_H__ */
