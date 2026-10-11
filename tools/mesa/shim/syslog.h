/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	syslog.h
 *	The system log as Mesa's logger names it. A TessronOS process has
 *	the console instead: lib/libgl/gl_os.c writes each message there.
 */

#ifndef TS_SHIM_SYSLOG_H
#define TS_SHIM_SYSLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_EMERG	0
#define LOG_ALERT	1
#define LOG_CRIT	2
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7

#define LOG_PID		0x01
#define LOG_CONS	0x02
#define LOG_NDELAY	0x08
#define LOG_USER	( 1 << 3 )

void	openlog( const char *ident, int option, int facility );
void	syslog( int priority, const char *fmt, ... ) __attribute__((format(printf, 2, 3)));
void	closelog( void );

#ifdef __cplusplus
}
#endif

#endif /* TS_SHIM_SYSLOG_H */
