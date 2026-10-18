/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	rtc.c (x64 PC)
 *	Real time clock: the CMOS clock (ports 70h, 71h), in UTC. Its
 *	registers hold BCD or binary, the hour in 12 or 24 hour form, as
 *	register B says; the century is in the register the FADT names, or
 *	taken as 20 when there is none. A read is taken twice to the same
 *	value, outside an update.
 */

#include <sys/machine.h>

#ifdef X64

#include "kernel.h"
#include "sysdepend.h"

#define CMOS_ADDR	0x70
#define CMOS_DATA	0x71

LOCAL UB cmos_rd( UB reg )
{
	out_port_b(CMOS_ADDR, reg);
	return in_port_b(CMOS_DATA);
}

LOCAL void cmos_wr( UB reg, UB v )
{
	out_port_b(CMOS_ADDR, reg);
	out_port_b(CMOS_DATA, v);
}

LOCAL INT bcd( INT v )
{
	return ( v >> 4 ) * 10 + ( v & 0xf );
}

LOCAL INT tobcd( INT v )
{
	return ( ( v / 10 ) << 4 ) | ( v % 10 );
}

/* Days from 1970-01-01 to y-m-d */
LOCAL D days_from_civil( INT y, INT m, INT d )
{
	INT	era, yoe, doy, doe;

	y -= ( m <= 2 );
	era = ( y >= 0 ? y : y - 399 ) / 400;
	yoe = y - era * 400;
	doy = ( 153 * ( m + ( m > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (D)era * 146097 + doe - 719468;
}

LOCAL void civil_from_days( D z, INT *py, INT *pm, INT *pd )
{
	D	era;
	INT	doe, yoe, doy, mp, y;

	z += 719468;
	era = ( z >= 0 ? z : z - 146096 ) / 146097;
	doe = (INT)( z - era * 146097 );
	yoe = ( doe - doe / 1460 + doe / 36524 - doe / 146096 ) / 365;
	y = yoe + (INT)era * 400;
	doy = doe - ( 365 * yoe + yoe / 4 - yoe / 100 );
	mp = ( 5 * doy + 2 ) / 153;
	*pd = doy - ( 153 * mp + 2 ) / 5 + 1;
	*pm = mp + ( mp < 10 ? 3 : -9 );
	*py = y + ( *pm <= 2 );
}

typedef struct {
	INT	sec, min, hour, day, mon, year, cent;
} T_CMOSTM;

LOCAL void read_raw( T_CMOSTM *t )
{
	UB	c = knl_acpi.century;

	while ( ( cmos_rd(0x0a) & 0x80 ) != 0 ) {
		;			/* an update is under way */
	}
	t->sec = cmos_rd(0x00);
	t->min = cmos_rd(0x02);
	t->hour = cmos_rd(0x04);
	t->day = cmos_rd(0x07);
	t->mon = cmos_rd(0x08);
	t->year = cmos_rd(0x09);
	t->cent = ( c != 0 ) ? cmos_rd(c) : -1;
}

EXPORT ER knl_rtc_read( UD *p_sec )
{
	T_CMOSTM	a, b;
	UB		regb;
	INT		i, pm, year;

	for ( i = 0; i < 10; i++ ) {
		read_raw(&a);
		read_raw(&b);
		if ( a.sec == b.sec && a.min == b.min && a.hour == b.hour && a.day == b.day
		  && a.mon == b.mon && a.year == b.year && a.cent == b.cent ) {
			break;
		}
	}
	if ( i == 10 ) {
		return E_IO;
	}
	regb = cmos_rd(0x0b);
	pm = ( a.hour & 0x80 ) != 0;
	a.hour &= 0x7f;
	if ( ( regb & 0x04 ) == 0 ) {		/* BCD */
		a.sec = bcd(a.sec);
		a.min = bcd(a.min);
		a.hour = bcd(a.hour);
		a.day = bcd(a.day);
		a.mon = bcd(a.mon);
		a.year = bcd(a.year);
		if ( a.cent >= 0 ) a.cent = bcd(a.cent);
	}
	if ( ( regb & 0x02 ) == 0 ) {		/* 12 hour form */
		a.hour %= 12;
		if ( pm ) a.hour += 12;
	}
	year = ( a.cent > 0 ? a.cent : 20 ) * 100 + a.year;
	if ( a.mon < 1 || a.mon > 12 || a.day < 1 || a.day > 31 || a.hour > 23 || a.min > 59 || a.sec > 59 ) {
		return E_IO;
	}
	*p_sec = (UD)( days_from_civil(year, a.mon, a.day) * 86400
		     + a.hour * 3600 + a.min * 60 + a.sec );
	return E_OK;
}

EXPORT ER knl_rtc_write( UD sec )
{
	INT	y, m, d, s = (INT)( sec % 86400 );
	UB	regb = cmos_rd(0x0b);
	INT	v[7], i;
	static const UB	reg[6] = { 0x00, 0x02, 0x04, 0x07, 0x08, 0x09 };

	civil_from_days((D)( sec / 86400 ), &y, &m, &d);
	v[0] = s % 60;
	v[1] = ( s / 60 ) % 60;
	v[2] = s / 3600;
	v[3] = d;
	v[4] = m;
	v[5] = y % 100;
	v[6] = y / 100;
	cmos_wr(0x0b, regb | 0x80);		/* updates held off while it is written */
	for ( i = 0; i < 6; i++ ) {
		INT	x = v[i];

		if ( i == 2 && ( regb & 0x02 ) == 0 ) {	/* 12 hour form */
			x = ( v[2] % 12 == 0 ) ? 12 : v[2] % 12;
			x = ( ( regb & 0x04 ) == 0 ) ? tobcd(x) : x;
			if ( v[2] >= 12 ) x |= 0x80;
			cmos_wr(reg[i], (UB)x);
			continue;
		}
		cmos_wr(reg[i], (UB)( ( ( regb & 0x04 ) == 0 ) ? tobcd(x) : x ));
	}
	if ( knl_acpi.century != 0 ) {
		cmos_wr(knl_acpi.century, (UB)( ( ( regb & 0x04 ) == 0 ) ? tobcd(v[6]) : v[6] ));
	}
	cmos_wr(0x0b, regb & ~0x80);
	return E_OK;
}

#endif /* X64 */
