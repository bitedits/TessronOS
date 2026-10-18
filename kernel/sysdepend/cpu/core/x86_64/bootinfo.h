/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bootinfo.h (x86-64)
 *	What the UEFI loader (tools/x64/uefi) hands the kernel.
 *
 *	The loader is a PE32+ program built with the mingw-w64 compiler,
 *	where long is 32 bits, so everything here has a fixed size of its
 *	own: the same bytes on both sides. The kernel is entered in 64 bit
 *	mode with interrupts off, on the loader's page tables (the low half
 *	the identity map of RAM, the high half the kernel's layout of
 *	sysdef.h), with rdi the physical address of a T_BOOTINFO.
 */

#ifndef _SYSDEPEND_CPU_CORE_BOOTINFO_
#define _SYSDEPEND_CPU_CORE_BOOTINFO_

#ifdef _in_asm_source_
#define BI_C64(x)		x
#else
#define BI_C64(x)		x##ull
#endif

#define BI_MAGIC		0x4f42534f	/* "OSBO" */
#define BI_VERSION		1

/*
 * The head of the kernel image: the first bytes of its lowest loadable
 * segment. heap is how many bytes of free memory the kernel wants right
 * after its image, contiguous with it (the page frame database and the
 * Imalloc area, reset_main.c); the loader allocates the image and that
 * as one run of pages.
 */
#define KHDR_MAGIC		BI_C64(0x3436585f534f5354)	/* "TSOS_X64" */

#ifndef _in_asm_source_
typedef struct {
	unsigned long long	magic;		/* KHDR_MAGIC */
	unsigned long long	heap;		/* bytes wanted past the image (besides the database) */
	unsigned long long	db_per_page;	/* bytes of the database for each page of RAM */
	unsigned long long	reserved;
} T_KHDR;
#endif

/*
 * The memory map, the loader's reading of the firmware's: one entry for
 * a run of pages of one kind. Runs of the same kind that touch are made
 * one.
 */
#define BI_MEM_FREE		1	/* free for the kernel to use */
#define BI_MEM_KERNEL		2	/* the kernel's image, its heap, this information, page tables */
#define BI_MEM_ACPI		3	/* ACPI tables (reclaimable once read) */
#define BI_MEM_NVS		4	/* ACPI NVS: the firmware's own, kept */
#define BI_MEM_RESERVED		5	/* the firmware's runtime and reserved memory */
#define BI_MEM_MMIO		6	/* device registers */
#define BI_MEM_BAD		7	/* memory the firmware found faulty */

#define BI_MAX_MEM		128

#ifndef _in_asm_source_
typedef struct {
	unsigned long long	start;		/* physical, page aligned */
	unsigned long long	size;		/* bytes */
	unsigned int		type;		/* BI_MEM_* */
	unsigned int		pad;
} T_BIMEM;
#endif

/*
 * The screen the firmware set up (Graphics Output Protocol). format
 * says where red, green and blue are in a 32 bit pixel.
 */
#define BI_FB_NONE		0
#define BI_FB_BGRX		1	/* blue in the low byte (PixelBlueGreenRedReserved8BitPerColor) */
#define BI_FB_RGBX		2	/* red in the low byte */
#define BI_FB_MASK		3	/* the masks below say */

#ifndef _in_asm_source_
typedef struct {
	unsigned long long	pa;		/* where the pixels are */
	unsigned long long	size;		/* bytes */
	unsigned int		width;
	unsigned int		height;
	unsigned int		stride;		/* bytes from one row to the next */
	unsigned int		format;		/* BI_FB_* */
	unsigned int		mask_r, mask_g, mask_b, mask_x;
} T_BIFB;
#endif

#define BI_CMDLINE_LEN		512
#define BI_RNG_LEN		32

#ifndef _in_asm_source_
typedef struct {
	unsigned int		magic;		/* BI_MAGIC */
	unsigned int		version;	/* BI_VERSION */
	unsigned int		size;		/* sizeof(T_BOOTINFO) */
	unsigned int		nmem;		/* entries of mem[] in use */

	unsigned long long	kimg_pa;	/* where the kernel's image is, physically */
	unsigned long long	kimg_va;	/* the virtual address it is linked at (its lowest) */
	unsigned long long	kimg_size;	/* bytes of the image, its zeroed data included */
	unsigned long long	heap_pa;	/* the free memory after it (T_KHDR.heap and the database) */
	unsigned long long	heap_size;

	unsigned long long	pml4_pa;	/* the loader's page tables */
	unsigned long long	tramp_pa;	/* 4 pages below 1MB for starting the other processors */
	unsigned long long	rsdp_pa;	/* ACPI RSDP, 0 if none */
	unsigned long long	smbios_pa;	/* SMBIOS 2 entry point, 0 if none */
	unsigned long long	smbios3_pa;	/* SMBIOS 3 entry point, 0 if none */
	unsigned long long	ram_top;	/* end of the highest RAM, physically */
	unsigned long long	dmap_size;	/* bytes the high half's linear map covers from 0 */

	T_BIFB			fb;

	unsigned int		rng_len;	/* bytes of rng[] the firmware gave (EFI_RNG_PROTOCOL) */
	unsigned int		flags;		/* BI_F_* */
	unsigned char		rng[BI_RNG_LEN];
	char			cmdline[BI_CMDLINE_LEN];	/* the ESP's cmdline.txt, one line */
	char			fw_vendor[64];	/* the firmware's vendor, in ASCII */

	T_BIMEM			mem[BI_MAX_MEM];
} T_BOOTINFO;
#endif

#define BI_F_1GPAGE		0x0001	/* the linear map is made of 1GB pages */

#endif /* _SYSDEPEND_CPU_CORE_BOOTINFO_ */
