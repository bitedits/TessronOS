/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.00.00
 *
 *    Copyright (C) 2006-2019 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2019/12/11.
 *
 *----------------------------------------------------------------------
 */

/*
 *	tmonitor.h
 *
 *	T-Monitor compatible calls
 */

#ifndef __TM_TMONITOR_H__
#define __TM_TMONITOR_H__

#include <sys/machine.h>
#include <tk/typedef.h>


IMPORT void libtm_init(void);

/*
 * Monitor service function
 */
IMPORT INT  tm_getchar( INT wait );
IMPORT INT  tm_putchar( INT c );
IMPORT INT  tm_getline( UB *buff );
IMPORT INT  tm_putstring( const UB *buff );

/*
 * What went to the console, from *p_pos on: up to max bytes, and
 * *p_pos moved past them. A position too old is moved to the oldest
 * byte still kept. Answers how many bytes were given.
 */
IMPORT INT  tm_log_read( UD *p_pos, UB *buf, INT max );
IMPORT void tm_com_set_base( UBINT base );	/* move the console to another PL011 */
IMPORT INT  tm_printf( const UB *format, ... );
IMPORT INT  tm_sprintf( UB *str, const UB *format, ... );

/*
 * Urgent output (libtm.c): straight to the UART, without the console's
 * lock, for what is said as the system stops. tm_urgent_begin/end keep
 * the lines of one report together, with interrupts disabled between.
 */
IMPORT UINT tm_urgent_begin( void );
IMPORT void tm_urgent_end( UINT intsts );
IMPORT void tm_urgent_puts( const UB *s );
IMPORT INT  tm_printf_urgent( const UB *format, ... );

#endif /* __TM_TMONITOR_H__ */
