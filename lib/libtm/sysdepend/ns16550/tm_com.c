/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	tm_com.c
 *	T-Monitor communication: a 16550 at an I/O port (COM1 of a PC),
 *	polled. What goes out goes to the screen console too, when there
 *	is one (tm_mirror), so that a machine without a serial port shows
 *	the console from the first line.
 */

#include <tk/tkernel.h>

#if USE_TMONITOR
#include "../../libtm.h"

#ifdef CPU_CORE_X86_64
#if TM_COM_SERIAL_DEV

EXPORT UH	tm_uart_port = COM1_PORT;
EXPORT BOOL	tm_uart_present = FALSE;	/* the port answers (tm_com_init) */

/* The screen console (kernel/sysdepend/x64/fbcon.c), once it is set up */
EXPORT void	(*tm_mirror)( const UB *buf, INT size ) = NULL;

/* Bounded: a port that never says it has room is taken to be gone */
#define TX_SPIN		200000

EXPORT	void	tm_snd_dat( const UB* buf, INT size )
{
	if ( tm_mirror != NULL ) {
		(*tm_mirror)(buf, size);
	}
	if ( !tm_uart_present ) {
		return;
	}
	while ( size-- > 0 ) {
		INT	n = TX_SPIN;

		while ( ( in_port_b(tm_uart_port + UART_LSR) & UART_LSR_THRE ) == 0 && --n > 0 ) {
			;	/* transmit holding register full */
		}
		out_port_b(tm_uart_port + UART_THR, *buf++);
	}
}

EXPORT	void	tm_rcv_dat( UB* buf, INT size )
{
	while ( size-- > 0 ) {
		while ( !tm_uart_present
		     || ( in_port_b(tm_uart_port + UART_LSR) & UART_LSR_DR ) == 0 ) {
			;	/* nothing received */
		}
		*buf++ = in_port_b(tm_uart_port + UART_RBR);
	}
}

/*
 * 115200 8N1, FIFOs on, no interrupts. A port that is not there reads
 * as all ones.
 */
EXPORT	void	tm_com_init( void )
{
	UH	p = tm_uart_port;

	if ( in_port_b(p + UART_LSR) == 0xff ) {
		tm_uart_present = FALSE;
		return;
	}
	out_port_b(p + UART_IER, 0x00);
	out_port_b(p + UART_LCR, 0x80);		/* divisor latch */
	out_port_b(p + 0, 0x01);		/* 115200 */
	out_port_b(p + 1, 0x00);
	out_port_b(p + UART_LCR, 0x03);		/* 8N1 */
	out_port_b(p + UART_FCR, 0xc7);		/* FIFO on, cleared */
	out_port_b(p + UART_MCR, 0x03);		/* DTR, RTS */
	tm_uart_present = TRUE;
}

#endif /* TM_COM_SERIAL_DEV */
#endif /* CPU_CORE_X86_64 */
#endif /* USE_TMONITOR */
