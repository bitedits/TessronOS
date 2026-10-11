/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	utmpx.h
 *	The records of who is logged in (design 5.8.3). The system keeps
 *	none: a record written is dropped and reading finds no record.
 */

#ifndef _UTMPX_H_
#define _UTMPX_H_

#include <sys/types.h>
#include <sys/time.h>

#define _UTX_USERSIZE	32
#define _UTX_LINESIZE	32
#define _UTX_IDSIZE	4
#define _UTX_HOSTSIZE	256

#define EMPTY		0
#define RUN_LVL		1
#define BOOT_TIME	2
#define OLD_TIME	3
#define NEW_TIME	4
#define INIT_PROCESS	5
#define LOGIN_PROCESS	6
#define USER_PROCESS	7
#define DEAD_PROCESS	8
#define ACCOUNTING	9
#define SIGNATURE	10
#define DOWN_TIME	11

struct utmpx {
	char		ut_name[_UTX_USERSIZE];
	char		ut_id[_UTX_IDSIZE];
	char		ut_line[_UTX_LINESIZE];
	char		ut_host[_UTX_HOSTSIZE];
	uint16_t	ut_session;
	uint16_t	ut_type;
	pid_t		ut_pid;
	struct {
		uint16_t	e_termination;
		uint16_t	e_exit;
	} ut_exit;
	struct timeval	ut_tv;
};

#define ut_user		ut_name
#define ut_xtime	ut_tv.tv_sec

#ifdef __cplusplus
extern "C" {
#endif

void		setutxent( void );
void		endutxent( void );
struct utmpx	*getutxent( void );
struct utmpx	*getutxid( const struct utmpx * );
struct utmpx	*getutxline( const struct utmpx * );
struct utmpx	*pututxline( const struct utmpx * );
int		utmpxname( const char * );

#ifdef __cplusplus
}
#endif

#endif /* _UTMPX_H_ */
