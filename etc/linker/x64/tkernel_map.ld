/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	tkernel_map.ld
 *	Linker script for the x64 PC (x86-64)
 *
 *	The kernel is linked into the top 2GB of the address space
 *	(-mcmodel=kernel) at KIMG_VA_BASE + LINK_PA, and its load addresses
 *	(AT) are LINK_PA onwards. The UEFI loader lays the image out by
 *	the load addresses wherever it finds room and maps the kernel's
 *	segments at their virtual addresses (tools/x64/uefi/loader.c); the
 *	kernel learns where it is from the T_BOOTINFO (knl_kimg_off).
 *
 *	The first bytes are the image's head (.khdr, T_KHDR). Text, the
 *	read only data and the data are separate segments, so that text is
 *	read only and executable and the rest never executed (W^X, design
 *	5.11). The user section of the system process is linked at its
 *	address in the process space and loaded after the read only data.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KIMG_VA_BASE = 0xFFFFFFFF80000000;
LINK_PA = 0x200000;
BOOT_STACK_SIZE = 0x4000;

PHDRS
{
	text	PT_LOAD FLAGS(5);	/* R + X */
	rodata	PT_LOAD FLAGS(4);	/* R */
	user	PT_LOAD FLAGS(4);	/* the system process's section: mapped by space.c */
	data	PT_LOAD FLAGS(6);	/* R + W */
}

SECTIONS
{
	. = KIMG_VA_BASE + LINK_PA;
	__kernel_start = .;

	.text : AT(ADDR(.text) - KIMG_VA_BASE) {
		KEEP(*(.khdr))
		/*
		 * The desktop's code, data and zeroed data, each in one place:
		 * 強制終了 knows the desktop's tasks by where they start, and puts
		 * its variables back as they were when the machine started
		 * before it starts the desktop again (outer_kernel/wm/fquit.c).
		 * The test driver of the screen stays out: it goes on across.
		 */
		__dt_text_start = .;
		EXCLUDE_FILE(*/dtuitest.o) */application/desktop/*.o(.text .text.*)
		*/application/cab/*.o(.text .text.*)
		*/application/doc/*.o(.text .text.*)
		*/application/fig/*.o(.text .text.*)
		*/lib/librtad/*.o(.text .text.*)
		__dt_text_end = .;
		*(.text .text.*)
	} :text

	.rodata : AT(ADDR(.rodata) - KIMG_VA_BASE) ALIGN(0x1000) {
		*(.rodata .rodata.*)
		*(.gcc_except_table .gcc_except_table.*)
		*(.eh_frame .eh_frame_hdr)
	} :rodata
	__text_end = .;

	/* user section of the image (code and data of the system process, design 6.2.2) */
	__user_lma = ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 0x1000);
	.user 0x400000 : AT(__user_lma) ALIGN(0x1000) {
		__user_start = .;
		*(.user.text .user.text.*)
		. = ALIGN(0x1000);
		__user_text_end = .;
		*(.user.data .user.data.*)
		. = ALIGN(0x1000);
		__user_end = .;
	} :user
	. = KIMG_VA_BASE + __user_lma + SIZEOF(.user);

	.data : AT(ADDR(.data) - KIMG_VA_BASE) ALIGN(0x1000) {
		__data_start = .;
		/* the desktop's data (see .text) */
		__dt_data_start = .;
		EXCLUDE_FILE(*/dtuitest.o) */application/desktop/*.o(.data .data.*)
		*/application/cab/*.o(.data .data.*)
		*/application/doc/*.o(.data .data.*)
		*/application/fig/*.o(.data .data.*)
		*/lib/librtad/*.o(.data .data.*)
		. = ALIGN(16);
		__dt_data_end = .;
		*(.data .data.*)
		*(.got .got.*)
		. = ALIGN(16);
	} :data

	.noinit (NOLOAD) : AT(ADDR(.noinit) - KIMG_VA_BASE) ALIGN(16) {
		*(.noinit)
	} :data

	.bss (NOLOAD) : AT(ADDR(.bss) - KIMG_VA_BASE) ALIGN(16) {
		_bss_start = .;
		/* the desktop's zeroed data (see .text) */
		__dt_bss_start = .;
		EXCLUDE_FILE(*/dtuitest.o) */application/desktop/*.o(.bss .bss.*)
		*/application/cab/*.o(.bss .bss.*)
		*/application/doc/*.o(.bss .bss.*)
		*/application/fig/*.o(.bss .bss.*)
		*/lib/librtad/*.o(.bss .bss.*)
		. = ALIGN(16);
		__dt_bss_end = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(16);
		_bss_end = .;
	} :data

	.stack (NOLOAD) : AT(ADDR(.stack) - KIMG_VA_BASE) ALIGN(16) {
		__tmp_stack_end = .;
		. += BOOT_STACK_SIZE;
		__tmp_stack_start = .;
	} :data

	. = ALIGN(0x1000);
	__kernel_end = .;
	_HeapStart = .;

	/DISCARD/ : {
		*(.note.GNU-stack)
		*(.note.gnu.property)
		*(.comment)
	}

	_text_start	= ADDR(.text);
	_text_end	= _text_start + SIZEOF(.text);
	_data_org	= LOADADDR(.data);
	_data_start	= ADDR(.data);
	_data_end	= _data_start + SIZEOF(.data);
	_bss_size	= _bss_end - _bss_start;
}
