/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	gpio_bcm2711.c
 *	The general purpose pins of the BCM2711 (design 4.10.1, 7;
 *	docs/tessronos/03-platform.md 3.6).
 *
 *	One block holds all 58 pins. Each has three bits of function
 *	select (input, output or one of six alternate functions), a bit in
 *	the set, clear and level registers, and two bits of pull (none, up,
 *	down) that, unlike the older chips, can be read back. They are
 *	addressed here as bank 0, pin = the GPIO number:
 *
 *	   0..27	the 40 pin header
 *	  28..57	the board's own wiring: the Ethernet PHY's MDIO and
 *			RGMII, the Bluetooth UART, the Wi-Fi's SDIO, the
 *			audio PWM, the activity LED (42)
 *
 *	The activity LED is lit when its pin is high. On the Raspberry Pi
 *	400 the same pin is the power LED, which is left lit once the blinks
 *	that tell how far the start went are over (include/ts/board.h).
 *
 *	Read-modify-write of the function select and pull registers is done
 *	under a spin lock with interrupts off; the set and clear registers
 *	take a write of the one bit and need no lock.
 */

#include <sys/machine.h>

#ifdef RPI4

#include "kernel.h"
#include <tk/tkernel.h>
#include <ts/gpio.h>
#include <ts/board.h>
#include "sysdepend.h"

LOCAL T_SPLOCK	gpio_lock;

#define GPFSEL(n)		(GPIO_BASE + 0x00 + (UBINT)(n) * 4)	/* 10 pins each */
#define GPSET(n)		(GPIO_BASE + 0x1c + (UBINT)(n) * 4)	/* 32 pins each */
#define GPCLR(n)		(GPIO_BASE + 0x28 + (UBINT)(n) * 4)
#define GPLEV(n)		(GPIO_BASE + 0x34 + (UBINT)(n) * 4)
#define GPPUPPDN(n)		(GPIO_BASE + 0xe4 + (UBINT)(n) * 4)	/* 16 pins each */

/* Function select codes: 0 input, 1 output, then the alternates */
#define FSEL_IN			0
#define FSEL_OUT		1
#define FSEL_MASK		7

/* Pull codes */
#define PULL_NONE		0
#define PULL_UP			1
#define PULL_DOWN		2

LOCAL BOOL gpio_valid( UINT bank, UINT pin )
{
	return ( bank == 0 && pin < GPIO_NPINS );
}

LOCAL UW fsel_get( UINT pin )
{
	return ( in_w(GPFSEL(pin / 10)) >> ( ( pin % 10 ) * 3 ) ) & FSEL_MASK;
}

LOCAL void fsel_set( UINT pin, UW code )
{
	UINT	imask;
	UW	v;
	UINT	sh = ( pin % 10 ) * 3;

	ISpinLock(&gpio_lock, &imask);
	v = in_w(GPFSEL(pin / 10));
	v = ( v & ~( (UW)FSEL_MASK << sh ) ) | ( ( code & FSEL_MASK ) << sh );
	out_w(GPFSEL(pin / 10), v);
	ISpinUnlock(&gpio_lock, &imask);
}

EXPORT ER gpio_set_dir( UINT bank, UINT pin, UINT dir )
{
	if ( !gpio_valid(bank, pin) || dir > GPIO_DIR_OUT ) {
		return E_PAR;
	}
	fsel_set(pin, ( dir == GPIO_DIR_OUT ) ? FSEL_OUT : FSEL_IN);
	return E_OK;
}

EXPORT ER gpio_write( UINT bank, UINT pin, UINT value )
{
	if ( !gpio_valid(bank, pin) ) {
		return E_PAR;
	}
	if ( value != 0 ) {
		out_w(GPSET(pin / 32), 1UL << ( pin % 32 ));
	} else {
		out_w(GPCLR(pin / 32), 1UL << ( pin % 32 ));
	}
	return E_OK;
}

EXPORT INT gpio_read( UINT bank, UINT pin )
{
	if ( !gpio_valid(bank, pin) ) {
		return E_PAR;
	}
	return ( in_w(GPLEV(pin / 32)) & ( 1UL << ( pin % 32 ) ) ) ? 1 : 0;
}

/* Which way a pin goes: GPIO_DIR_IN or GPIO_DIR_OUT, or 2 + the alternate function it has */
EXPORT INT gpio_get_dir( UINT bank, UINT pin )
{
	UW	f;

	if ( !gpio_valid(bank, pin) ) {
		return E_PAR;
	}
	f = fsel_get(pin);
	switch ( f ) {
	case FSEL_IN:	return GPIO_DIR_IN;
	case FSEL_OUT:	return GPIO_DIR_OUT;
	case 4:		return 2 + 0;		/* alternate 0 .. 3 are 4 .. 7 */
	case 5:		return 2 + 1;
	case 6:		return 2 + 2;
	case 7:		return 2 + 3;
	case 3:		return 2 + 4;		/* alternate 4 */
	default:	return 2 + 5;		/* 2: alternate 5 */
	}
}

EXPORT ER gpio_set_pull( UINT bank, UINT pin, UINT pull )
{
	UINT	imask, sh;
	UW	v, code;

	if ( !gpio_valid(bank, pin) ) {
		return E_PAR;
	}
	switch ( pull ) {
	case GPIO_PULL_NONE:	code = PULL_NONE;	break;
	case GPIO_PULL_UP:	code = PULL_UP;		break;
	case GPIO_PULL_DOWN:	code = PULL_DOWN;	break;
	default:		return E_PAR;
	}
	sh = ( pin % 16 ) * 2;
	ISpinLock(&gpio_lock, &imask);
	v = in_w(GPPUPPDN(pin / 16));
	v = ( v & ~( 3UL << sh ) ) | ( code << sh );
	out_w(GPPUPPDN(pin / 16), v);
	ISpinUnlock(&gpio_lock, &imask);
	return E_OK;
}

EXPORT INT gpio_get_pull( UINT bank, UINT pin )
{
	UW	code;

	if ( !gpio_valid(bank, pin) ) {
		return E_PAR;
	}
	code = ( in_w(GPPUPPDN(pin / 16)) >> ( ( pin % 16 ) * 2 ) ) & 3;
	return ( code == PULL_UP ) ? GPIO_PULL_UP : ( code == PULL_DOWN ) ? GPIO_PULL_DOWN : GPIO_PULL_NONE;
}

/* How many pins a bank has, 0 for one there is not */
EXPORT INT gpio_bank_pins( UINT bank )
{
	return ( bank == 0 ) ? GPIO_NPINS : 0;
}

/*
 * The activity LED, lit when high. Once the object layer is up it is set
 * through the object of its bank (knl_obgpio_led); before that, while
 * the system starts, directly.
 */
IMPORT ER knl_obgpio_led( UINT pin, BOOL on );

EXPORT ER gpio_led( BOOL on )
{
	ER	er = knl_obgpio_led(GPIO_LED_ACT, on);

	if ( er != E_NOEXS ) {
		return er;
	}
	(void)gpio_write(0, GPIO_LED_ACT, on ? 1 : 0);
	return gpio_set_dir(0, GPIO_LED_ACT, GPIO_DIR_OUT);
}

/*
 * Blink the LED a given number of times. Used during bring-up to say
 * how far the kernel reached without a console: the count is the signal.
 * A board whose one LED is its power LED has it lit again afterwards.
 */
EXPORT void gpio_led_blink( INT times, UINT ms )
{
	INT	i;

	for ( i = 0; i < times; i++ ) {
		gpio_led(TRUE);
		tk_dly_tsk(ms);
		gpio_led(FALSE);
		tk_dly_tsk(ms);
	}
	if ( knl_board.led_power ) {
		gpio_led(TRUE);
	}
}

EXPORT ER knl_gpio_init( void )
{
	/* the LED pin an output, whatever the firmware left it as */
	(void)gpio_write(0, GPIO_LED_ACT, knl_board.led_power ? 1 : 0);
	return gpio_set_dir(0, GPIO_LED_ACT, GPIO_DIR_OUT);
}

#endif /* RPI4 */
