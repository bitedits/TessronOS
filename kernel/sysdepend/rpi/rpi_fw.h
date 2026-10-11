/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	rpi_fw.h
 *	The VideoCore firmware's property interface, shared by the Raspberry
 *	Pi 5 and the Raspberry Pi 4 (kernel/sysdepend/rpi/mbox.c).
 */

#ifndef _SYSDEPEND_RPI_FW_
#define _SYSDEPEND_RPI_FW_

/*
 * One call of the firmware's property interface. 'buf' is the whole
 * message, its size in bytes in word 0; it lies below 1GB physical and
 * is 16 byte aligned. Answers E_OK when the firmware accepted the
 * message, E_TMOUT when it did not answer.
 */
IMPORT ER knl_mbox_property( UW *buf );

/*
 * The same, for a call whose answer may take a refresh of the screen or
 * more: looked for without a pause for 'spin_us', then once a
 * millisecond with the calling task asleep. E_TMOUT after 'tmo_ms'.
 */
IMPORT ER knl_mbox_property_wait( UW *buf, UINT spin_us, UINT tmo_ms );

/* One value the firmware tells for a tag asked with one word (mbox.c) */
IMPORT ER knl_fw_value( UW tag, UW id, UW *p_val );

/* One value the firmware tells for a tag asked with nothing (mbox.c) */
IMPORT ER knl_fw_get( UW tag, UW *p_val );

/*
 * The processors' clock is set through include/ts/cpuclk.h: mbox.c gives
 * it the firmware's range, the setting of a rate, the rate now and what
 * the firmware says of the supply (knl_cpu_hw_*). The firmware starts
 * the processors slower than it allows and leaves the raising to the
 * operating system; it still lowers the clock by itself when the chip
 * gets too hot or the supply too low.
 */

#define TAG_GET_BOARD_REVISION	0x00010002U
#define TAG_GET_CLOCK_RATE	0x00030002U	/* asked with a clock's number, answers Hz */
#define TAG_GET_TEMPERATURE	0x00030006U	/* asked with 0, answers thousandths of a degree */
#define TAG_GET_MAX_CLOCK_RATE	0x00030004U	/* the same, the most it may be set to */
#define TAG_GET_MIN_CLOCK_RATE	0x00030007U	/* the same, the least */
#define TAG_GET_THROTTLED	0x00030046U	/* asked with 0: the supply and the clock (include/ts/cpuclk.h) */
#define TAG_SET_CLOCK_RATE	0x00038002U	/* a clock's number, Hz, 0: answers Hz */
#define TAG_NOTIFY_XHCI_RESET	0x00030058U	/* asked with a PCIe function's address */

/* Clock numbers of the property interface */
#define FW_CLOCK_EMMC		1
#define FW_CLOCK_UART		2
#define FW_CLOCK_ARM		3
#define FW_CLOCK_CORE		4
#define FW_CLOCK_EMMC2		12

#endif /* _SYSDEPEND_RPI_FW_ */
