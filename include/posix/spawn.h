/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	spawn.h
 *	newlib's posix_spawn, with the session of its own a shell's child
 *	may be started in (design 5.8.2)
 */

#ifndef __POSIX_SPAWN_H__
#define __POSIX_SPAWN_H__

#include <signal.h>
#include_next <spawn.h>

#define POSIX_SPAWN_SETSID		0x80
#define POSIX_SPAWN_RETURNERROR		0x40
#define POSIX_SPAWN_TCSETPGROUP		0x100	/* its group made the terminal's foreground before it runs */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Signals the child ignores although the caller catches them (a shell's
 * job in the background without job control ignores ^C): the child's
 * ignored ones are these and the ones the caller ignores.
 */
int	posix_spawnattr_setsigignore_np( posix_spawnattr_t *, const sigset_t * );
int	posix_spawnattr_getsigignore_np( const posix_spawnattr_t *, sigset_t * );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_SPAWN_H__ */
