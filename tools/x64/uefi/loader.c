/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	loader.c
 *	The UEFI loader of TessronOS on x64 (\EFI\BOOT\BOOTX64.EFI).
 *
 *	It reads the kernel (tessronos.elf) and the one line of start-up
 *	settings (cmdline.txt) from the root of the volume it was itself
 *	read from, sets the screen up through the Graphics Output Protocol,
 *	notes what the kernel needs of the firmware (the memory map, the
 *	ACPI and SMBIOS tables, a seed for the random numbers), builds the
 *	page tables of the kernel's address space, leaves the boot services
 *	and jumps to the kernel with the physical address of a T_BOOTINFO
 *	(kernel/sysdepend/cpu/core/x86_64/bootinfo.h) in rdi.
 *
 *	The firmware's runtime services are not used afterwards: the kernel
 *	reads the clock from the CMOS and powers off through ACPI.
 *
 *	What it says goes to the firmware's console and to COM1, where a
 *	machine has one.
 */

#include "efi.h"
#include "../../../kernel/sysdepend/cpu/core/x86_64/bootinfo.h"

/* What the loader allocates for the kernel is of this type, one of the OS's own */
#define MEM_TESSRON		0x80000001u

#define KIMG_VA_BASE		0xFFFFFFFF80000000ull
#define KVA_BASE		0xFFFF800000000000ull
#define DEV_VA_BASE		0xFFFFC00000000000ull

#define PG_P			0x001ull
#define PG_RW			0x002ull
#define PG_US			0x004ull
#define PG_PWT			0x008ull
#define PG_PCD			0x010ull
#define PG_A			0x020ull
#define PG_D			0x040ull
#define PG_PS			0x080ull
#define PG_G			0x100ull
#define PG_NX			0x8000000000000000ull
#define PG_TABLE		( PG_P | PG_RW | PG_US )

#define MSR_EFER		0xc0000080u
#define MSR_PAT			0x00000277u
#define EFER_NXE		0x800ull
#define PAT_VALUE		0x0007010600070106ull	/* WB, WC, UC-, UC, WB, WC, UC-, UC */

#define GB			0x40000000ull
#define MB2			0x200000ull

static EFI_SYSTEM_TABLE		*ST;
static EFI_BOOT_SERVICES	*BS;
static EFI_HANDLE		Image;

/* ------------------------------------------------------------------ helpers */

void *memset( void *d, int c, UINTN n )
{
	UINT8	*p = d;

	while ( n-- > 0 ) *p++ = (UINT8)c;
	return d;
}

void *memcpy( void *d, const void *s, UINTN n )
{
	UINT8		*p = d;
	const UINT8	*q = s;

	while ( n-- > 0 ) *p++ = *q++;
	return d;
}

static int guid_eq( const EFI_GUID *a, const EFI_GUID *b )
{
	const UINT8	*p = (const UINT8 *)a, *q = (const UINT8 *)b;
	int		i;

	for ( i = 0; i < 16; i++ ) {
		if ( p[i] != q[i] ) return 0;
	}
	return 1;
}

static inline void outb( UINT16 port, UINT8 v )
{
	__asm__ volatile ( "outb %0, %1" :: "a"(v), "Nd"(port) );
}

static inline UINT8 inb( UINT16 port )
{
	UINT8	v;

	__asm__ volatile ( "inb %1, %0" : "=a"(v) : "Nd"(port) );
	return v;
}

static inline UINT64 rdmsr( UINT32 msr )
{
	UINT32	lo, hi;

	__asm__ volatile ( "rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) );
	return ( (UINT64)hi << 32 ) | lo;
}

static inline void wrmsr( UINT32 msr, UINT64 v )
{
	__asm__ volatile ( "wrmsr" :: "c"(msr), "a"((UINT32)v), "d"((UINT32)( v >> 32 )) );
}

static inline void cpuid( UINT32 leaf, UINT32 *a, UINT32 *b, UINT32 *c, UINT32 *d )
{
	__asm__ volatile ( "cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0) );
}

/*
 * COM1, once the boot services are left (before, the firmware's console
 * goes there itself where there is one). Where there is none the port
 * reads as all ones and the wait for room ends at once; it is bounded
 * besides.
 */
static int	com1_ok = 0;

static void com1_init( void )
{
	outb(0x3f8 + 1, 0x00);		/* no interrupts */
	outb(0x3f8 + 3, 0x80);		/* divisor latch */
	outb(0x3f8 + 0, 0x01);		/* 115200 */
	outb(0x3f8 + 1, 0x00);
	outb(0x3f8 + 3, 0x03);		/* 8N1 */
	outb(0x3f8 + 2, 0xc7);		/* FIFO on, cleared */
	outb(0x3f8 + 4, 0x03);		/* DTR, RTS */
	com1_ok = ( inb(0x3f8 + 5) != 0xff );
}

static void com1_putc( char c )
{
	int	n = 100000;

	if ( !com1_ok ) return;
	while ( ( inb(0x3f8 + 5) & 0x20 ) == 0 && --n > 0 ) {
		;
	}
	outb(0x3f8, (UINT8)c);
}

static int	booted_out = 0;		/* the boot services have been left */

static void say( const char *s )
{
	CHAR16	buf[128];
	int	n = 0;

	for ( ; *s != '\0'; s++ ) {
		if ( booted_out ) {
			if ( *s == '\n' ) com1_putc('\r');
			com1_putc(*s);
			continue;
		}
		if ( *s == '\n' ) buf[n++] = L'\r';
		buf[n++] = (CHAR16)(UINT8)*s;
		if ( n >= 120 ) {
			buf[n] = 0;
			ST->ConOut->OutputString(ST->ConOut, buf);
			n = 0;
		}
	}
	if ( n > 0 && !booted_out ) {
		buf[n] = 0;
		ST->ConOut->OutputString(ST->ConOut, buf);
	}
}

static void say_hex( UINT64 v )
{
	char	b[19];
	int	i;

	b[0] = '0';
	b[1] = 'x';
	for ( i = 0; i < 16; i++ ) {
		b[2 + i] = "0123456789abcdef"[( v >> ( 60 - 4 * i ) ) & 0xf];
	}
	b[18] = '\0';
	say(b);
}

static void say_dec( UINT64 v )
{
	char	b[21];
	int	i = 20;

	b[i] = '\0';
	do {
		b[--i] = (char)( '0' + v % 10 );
		v /= 10;
	} while ( v != 0 );
	say(&b[i]);
}

static void stop( const char *why, EFI_STATUS st )
{
	say("TessronOS loader: ");
	say(why);
	if ( st != 0 ) {
		say(" (status ");
		say_hex(st);
		say(")");
	}
	say("\n");
	for ( ;; ) {
		__asm__ volatile ( "hlt" );
	}
}

static void *alloc_pages( UINTN n, UINT32 type )
{
	EFI_PHYSICAL_ADDRESS	a = 0;
	EFI_STATUS		st = BS->AllocatePages(AllocateAnyPages, type, n, &a);

	if ( EFI_ERROR(st) ) {
		say("TessronOS loader: no room for ");
		say_dec(n);
		say(" pages\n");
		stop("out of memory", st);
	}
	memset((void *)a, 0, n * EFI_PAGE_SIZE);
	return (void *)a;
}

/* ------------------------------------------------------------------ files */

static EFI_FILE_PROTOCOL *open_root( void )
{
	EFI_GUID			li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_GUID			fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL	*li;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL	*fs;
	EFI_FILE_PROTOCOL		*root;
	EFI_STATUS			st;

	st = BS->HandleProtocol(Image, &li_guid, (void **)&li);
	if ( EFI_ERROR(st) ) stop("no loaded image protocol", st);
	st = BS->HandleProtocol(li->DeviceHandle, &fs_guid, (void **)&fs);
	if ( EFI_ERROR(st) ) stop("the volume the loader came from has no file system", st);
	st = fs->OpenVolume(fs, &root);
	if ( EFI_ERROR(st) ) stop("cannot open the volume", st);
	return root;
}

/* A whole file read into memory the boot services give; NULL when there is no such file */
static UINT8 *read_file( EFI_FILE_PROTOCOL *root, CHAR16 *name, UINTN *p_size )
{
	EFI_FILE_PROTOCOL	*f;
	UINT64			size = 0;
	UINT8			*buf;
	UINTN			n;
	EFI_STATUS		st;

	st = root->Open(root, &f, name, EFI_FILE_MODE_READ, 0);
	if ( EFI_ERROR(st) ) return 0;
	f->SetPosition(f, 0xFFFFFFFFFFFFFFFFull);	/* to the end: its size */
	f->GetPosition(f, &size);
	f->SetPosition(f, 0);
	st = BS->AllocatePool(EfiLoaderData, size + 1, (void **)&buf);
	if ( EFI_ERROR(st) ) stop("out of memory for a file", st);
	n = size;
	st = f->Read(f, &n, buf);
	f->Close(f);
	if ( EFI_ERROR(st) || n != size ) stop("cannot read a file", st);
	buf[size] = 0;
	*p_size = size;
	return buf;
}

/* ------------------------------------------------------------------ ELF */

typedef struct {
	UINT8	e_ident[16];
	UINT16	e_type;
	UINT16	e_machine;
	UINT32	e_version;
	UINT64	e_entry;
	UINT64	e_phoff;
	UINT64	e_shoff;
	UINT32	e_flags;
	UINT16	e_ehsize;
	UINT16	e_phentsize;
	UINT16	e_phnum;
	UINT16	e_shentsize;
	UINT16	e_shnum;
	UINT16	e_shstrndx;
} ELF64_EHDR;

typedef struct {
	UINT32	p_type;
	UINT32	p_flags;
	UINT64	p_offset;
	UINT64	p_vaddr;
	UINT64	p_paddr;
	UINT64	p_filesz;
	UINT64	p_memsz;
	UINT64	p_align;
} ELF64_PHDR;

#define PT_LOAD		1
#define PF_X		1
#define PF_W		2
#define EM_X86_64	62

/* ------------------------------------------------------------------ the memory map */

static UINT32 bi_type( UINT32 efi )
{
	switch ( efi ) {
	case EfiLoaderCode:
	case EfiLoaderData:
	case EfiBootServicesCode:
	case EfiBootServicesData:
	case EfiConventionalMemory:
		return BI_MEM_FREE;
	case EfiACPIReclaimMemory:
		return BI_MEM_ACPI;
	case EfiACPIMemoryNVS:
		return BI_MEM_NVS;
	case EfiMemoryMappedIO:
	case EfiMemoryMappedIOPortSpace:
		return BI_MEM_MMIO;
	case EfiUnusableMemory:
		return BI_MEM_BAD;
	case MEM_TESSRON:
		return BI_MEM_KERNEL;
	default:
		return BI_MEM_RESERVED;
	}
}

/* RAM the firmware or the kernel uses: the linear map covers it */
static int is_ram( UINT32 efi )
{
	switch ( efi ) {
	case EfiLoaderCode:
	case EfiLoaderData:
	case EfiBootServicesCode:
	case EfiBootServicesData:
	case EfiRuntimeServicesCode:
	case EfiRuntimeServicesData:
	case EfiConventionalMemory:
	case EfiACPIReclaimMemory:
	case EfiACPIMemoryNVS:
	case MEM_TESSRON:
		return 1;
	default:
		return 0;
	}
}

static UINT8	*mmap_buf;
static UINTN	mmap_cap, mmap_size, mmap_key, mmap_dsize;

static void get_map( void )
{
	UINT32		ver;
	EFI_STATUS	st;

	mmap_size = mmap_cap;
	st = BS->GetMemoryMap(&mmap_size, (EFI_MEMORY_DESCRIPTOR *)mmap_buf, &mmap_key, &mmap_dsize, &ver);
	if ( EFI_ERROR(st) ) stop("cannot get the memory map", st);
}

static void alloc_map( void )
{
	UINTN		size = 0, key, dsize;
	UINT32		ver;
	EFI_STATUS	st;

	st = BS->GetMemoryMap(&size, 0, &key, &dsize, &ver);
	if ( st != EFI_BUFFER_TOO_SMALL ) stop("cannot size the memory map", st);
	/* room for what allocating this and the rest adds */
	mmap_cap = size + 64 * dsize;
	st = BS->AllocatePool(EfiLoaderData, mmap_cap, (void **)&mmap_buf);
	if ( EFI_ERROR(st) ) stop("out of memory for the memory map", st);
}

static UINT64 ram_top( void )
{
	UINT64	top = 0;
	UINTN	off;

	get_map();
	for ( off = 0; off + mmap_dsize <= mmap_size; off += mmap_dsize ) {
		EFI_MEMORY_DESCRIPTOR	*d = (EFI_MEMORY_DESCRIPTOR *)( mmap_buf + off );
		UINT64			e = d->PhysicalStart + d->NumberOfPages * EFI_PAGE_SIZE;

		if ( is_ram(d->Type) && e > top ) top = e;
	}
	return top;
}

/* After leaving the boot services: the firmware's map in the kernel's terms */
static void convert_map( T_BOOTINFO *bi )
{
	UINTN	off;
	UINT32	n = 0;

	for ( off = 0; off + mmap_dsize <= mmap_size; off += mmap_dsize ) {
		EFI_MEMORY_DESCRIPTOR	*d = (EFI_MEMORY_DESCRIPTOR *)( mmap_buf + off );
		UINT64			s = d->PhysicalStart, size = d->NumberOfPages * EFI_PAGE_SIZE;
		UINT32			t = bi_type(d->Type);

		if ( size == 0 ) continue;
		if ( n > 0 && bi->mem[n - 1].type == t && bi->mem[n - 1].start + bi->mem[n - 1].size == s ) {
			bi->mem[n - 1].size += size;
			continue;
		}
		if ( n >= BI_MAX_MEM ) {
			continue;		/* more kinds than room: the kernel sees what fits */
		}
		bi->mem[n].start = s;
		bi->mem[n].size = size;
		bi->mem[n].type = t;
		bi->mem[n].pad = 0;
		n++;
	}
	bi->nmem = n;
}

/* ------------------------------------------------------------------ page tables */

static UINT64	*pt_pool;
static UINTN	pt_used, pt_cap;

static UINT64 *pt_new( void )
{
	UINT64	*p;

	if ( pt_used >= pt_cap ) stop("page tables: out of pool", 0);
	p = pt_pool + pt_used * 512;
	pt_used++;
	return p;
}

#define IDX(va, s)	( ( (va) >> (s) ) & 511 )

/* The table under entry e of tbl, made when there is none */
static UINT64 *pt_next( UINT64 *tbl, UINTN i )
{
	if ( ( tbl[i] & PG_P ) == 0 ) {
		tbl[i] = (UINT64)pt_new() | PG_TABLE;
	}
	return (UINT64 *)( tbl[i] & 0x000FFFFFFFFFF000ull );
}

/* [va, va + size) to [pa, ...) in large pages (1GB where allowed, else 2MB) */
static void map_large( UINT64 *pml4, UINT64 va, UINT64 pa, UINT64 size, UINT64 attr, int gb )
{
	UINT64	end = va + size;

	while ( va < end ) {
		UINT64	*pdpt = pt_next(pml4, IDX(va, 39));

		if ( gb && ( va & ( GB - 1 ) ) == 0 && ( pa & ( GB - 1 ) ) == 0 && end - va >= GB ) {
			pdpt[IDX(va, 30)] = pa | attr | PG_PS;
			va += GB;
			pa += GB;
		} else {
			UINT64	*pd = pt_next(pdpt, IDX(va, 30));

			pd[IDX(va, 21)] = pa | attr | PG_PS;
			va += MB2;
			pa += MB2;
		}
	}
}

/* 4KB pages */
static void map_pages( UINT64 *pml4, UINT64 va, UINT64 pa, UINT64 size, UINT64 attr )
{
	UINT64	end = va + size;

	for ( ; va < end; va += EFI_PAGE_SIZE, pa += EFI_PAGE_SIZE ) {
		UINT64	*pdpt = pt_next(pml4, IDX(va, 39));
		UINT64	*pd = pt_next(pdpt, IDX(va, 30));
		UINT64	*pt = pt_next(pd, IDX(va, 21));

		pt[IDX(va, 12)] = pa | attr;
	}
}

/* ------------------------------------------------------------------ the screen */

static UINT32 parse_uint( const char **pp )
{
	const char	*p = *pp;
	UINT32		v = 0;

	while ( *p >= '0' && *p <= '9' ) v = v * 10 + (UINT32)( *p++ - '0' );
	*pp = p;
	return v;
}

/* "video=WxH" of the start-up settings, when there */
static void want_video( const char *cmd, UINT32 *w, UINT32 *h )
{
	const char	*p;

	*w = *h = 0;
	for ( p = cmd; *p != '\0'; p++ ) {
		if ( p[0] == 'v' && p[1] == 'i' && p[2] == 'd' && p[3] == 'e' && p[4] == 'o' && p[5] == '=' ) {
			p += 6;
			*w = parse_uint(&p);
			if ( *p == 'x' ) {
				p++;
				*h = parse_uint(&p);
			}
			return;
		}
	}
}

/*
 * The mode the kernel draws in: the one the settings name, else the
 * largest that fits 1920x1280 (the panel of the Surface Go 2), of the
 * modes whose pixels can be written directly
 */
static void setup_screen( T_BOOTINFO *bi )
{
	EFI_GUID				guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL		*gop;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION	*info;
	EFI_STATUS				st;
	UINT32					m, best = 0xffffffff, ww, wh;
	UINT64					best_area = 0;
	UINTN					sz;

	st = BS->LocateProtocol(&guid, 0, (void **)&gop);
	if ( EFI_ERROR(st) ) {
		say("TessronOS loader: no graphics output\n");
		return;
	}
	want_video(bi->cmdline, &ww, &wh);
	for ( m = 0; m < gop->Mode->MaxMode; m++ ) {
		UINT64	area;

		if ( EFI_ERROR(gop->QueryMode(gop, m, &sz, &info)) ) continue;
		if ( info->PixelFormat == PixelBltOnly ) continue;
		if ( ww != 0 ) {
			if ( info->HorizontalResolution == ww && info->VerticalResolution == wh ) {
				best = m;
				break;
			}
			continue;
		}
		if ( info->HorizontalResolution > 1920 || info->VerticalResolution > 1280 ) continue;
		area = (UINT64)info->HorizontalResolution * info->VerticalResolution;
		if ( area > best_area ) {
			best_area = area;
			best = m;
		}
	}
	if ( best != 0xffffffff && best != gop->Mode->Mode ) {
		st = gop->SetMode(gop, best);
		if ( EFI_ERROR(st) ) say("TessronOS loader: the screen mode was not set\n");
	}
	info = gop->Mode->Info;
	if ( info->PixelFormat == PixelBltOnly ) {
		say("TessronOS loader: the screen cannot be written directly\n");
		return;
	}
	bi->fb.pa = gop->Mode->FrameBufferBase;
	bi->fb.size = gop->Mode->FrameBufferSize;
	bi->fb.width = info->HorizontalResolution;
	bi->fb.height = info->VerticalResolution;
	bi->fb.stride = info->PixelsPerScanLine * 4;
	bi->fb.format = ( info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ) ? BI_FB_BGRX
		      : ( info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ) ? BI_FB_RGBX
		      : BI_FB_MASK;
	bi->fb.mask_r = info->PixelInformation.RedMask;
	bi->fb.mask_g = info->PixelInformation.GreenMask;
	bi->fb.mask_b = info->PixelInformation.BlueMask;
	bi->fb.mask_x = info->PixelInformation.ReservedMask;
	say("TessronOS loader: screen ");
	say_dec(bi->fb.width);
	say("x");
	say_dec(bi->fb.height);
	say(" at ");
	say_hex(bi->fb.pa);
	say("\n");
}

/* ------------------------------------------------------------------ the firmware's tables */

static void find_tables( T_BOOTINFO *bi )
{
	EFI_GUID	acpi20 = EFI_ACPI_20_TABLE_GUID, acpi10 = EFI_ACPI_10_TABLE_GUID;
	EFI_GUID	smb = EFI_SMBIOS_TABLE_GUID, smb3 = EFI_SMBIOS3_TABLE_GUID;
	UINTN		i;

	for ( i = 0; i < ST->NumberOfTableEntries; i++ ) {
		EFI_CONFIGURATION_TABLE	*t = &ST->ConfigurationTable[i];
		UINT64			a = (UINT64)t->VendorTable;

		if ( guid_eq(&t->VendorGuid, &acpi20) ) {
			bi->rsdp_pa = a;
		} else if ( guid_eq(&t->VendorGuid, &acpi10) && bi->rsdp_pa == 0 ) {
			bi->rsdp_pa = a;
		} else if ( guid_eq(&t->VendorGuid, &smb) ) {
			bi->smbios_pa = a;
		} else if ( guid_eq(&t->VendorGuid, &smb3) ) {
			bi->smbios3_pa = a;
		}
	}
}

static void get_seed( T_BOOTINFO *bi )
{
	EFI_GUID		guid = EFI_RNG_PROTOCOL_GUID;
	EFI_RNG_PROTOCOL	*rng;

	if ( EFI_ERROR(BS->LocateProtocol(&guid, 0, (void **)&rng)) ) return;
	if ( !EFI_ERROR(rng->GetRNG(rng, 0, BI_RNG_LEN, bi->rng)) ) {
		bi->rng_len = BI_RNG_LEN;
	}
}

/* ------------------------------------------------------------------ main */

EFI_STATUS EFIAPI efi_main( EFI_HANDLE image, EFI_SYSTEM_TABLE *st )
{
	EFI_FILE_PROTOCOL	*root;
	T_BOOTINFO		*bi;
	UINT8			*elf, *cmd, *kimg;
	UINTN			elf_size, cmd_size, i;
	ELF64_EHDR		*eh;
	ELF64_PHDR		*ph;
	UINT64			lo = ~0ull, hi = 0, kva = 0, span, top, heap, dmap, kpa;
	UINT64			*pml4, tramp = 0x9f000;
	T_KHDR			*kh;
	UINT32			a, b, c, d;
	int			gb;
	EFI_STATUS		s;

	ST = st;
	BS = st->BootServices;
	Image = image;
	BS->SetWatchdogTimer(0, 0, 0, 0);
	com1_init();
	say("\nTessronOS loader\n");

	bi = alloc_pages(( sizeof(T_BOOTINFO) + EFI_PAGE_SIZE - 1 ) / EFI_PAGE_SIZE, MEM_TESSRON);
	bi->magic = BI_MAGIC;
	bi->version = BI_VERSION;
	bi->size = sizeof(T_BOOTINFO);
	for ( i = 0; i < sizeof(bi->fw_vendor) - 1 && st->FirmwareVendor[i] != 0; i++ ) {
		CHAR16	ch = st->FirmwareVendor[i];

		bi->fw_vendor[i] = ( ch < 0x80 ) ? (char)ch : '?';
	}

	root = open_root();
	cmd = read_file(root, L"\\cmdline.txt", &cmd_size);
	if ( cmd != 0 ) {
		for ( i = 0; i < cmd_size && i < BI_CMDLINE_LEN - 1 && cmd[i] != '\n' && cmd[i] != '\r'; i++ ) {
			bi->cmdline[i] = (char)cmd[i];
		}
		say("TessronOS loader: settings \"");
		say(bi->cmdline);
		say("\"\n");
	}
	elf = read_file(root, L"\\tessronos.elf", &elf_size);
	if ( elf == 0 ) stop("no \\tessronos.elf", 0);

	/* the kernel: a 64 bit executable for this machine */
	eh = (ELF64_EHDR *)elf;
	if ( elf_size < sizeof(*eh) || eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E'
	  || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' || eh->e_ident[4] != 2
	  || eh->e_machine != EM_X86_64 || eh->e_type != 2 ) {
		stop("tessronos.elf is not an x86-64 kernel", 0);
	}
	/*
	 * The image is laid out by the segments' physical addresses (the
	 * linker's load addresses), which keep the kernel's sections and
	 * the user section of the system process (linked at a low address
	 * of the process space, design 6.2.2) in one run; it is placed
	 * wherever there is room, and only the kernel's segments are mapped
	 * at their virtual addresses
	 */
	ph = (ELF64_PHDR *)( elf + eh->e_phoff );
	for ( i = 0; i < eh->e_phnum; i++ ) {
		if ( ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0 ) continue;
		if ( ph[i].p_paddr < lo ) {
			lo = ph[i].p_paddr;
			kva = ph[i].p_vaddr;
		}
		if ( ph[i].p_paddr + ph[i].p_memsz > hi ) hi = ph[i].p_paddr + ph[i].p_memsz;
	}
	if ( kva < KIMG_VA_BASE || ( lo & ( EFI_PAGE_SIZE - 1 ) ) != 0 || hi <= lo ) {
		stop("tessronos.elf is not linked where the kernel goes", 0);
	}
	span = ( hi - lo + EFI_PAGE_SIZE - 1 ) & ~(UINT64)( EFI_PAGE_SIZE - 1 );

	/* how much RAM there is decides the database and the linear map */
	alloc_map();
	top = ram_top();
	cpuid(0x80000001, &a, &b, &c, &d);
	gb = ( d >> 26 ) & 1;			/* 1GB pages */
	dmap = ( top + GB - 1 ) & ~( GB - 1 );
	if ( dmap < 4 * GB ) dmap = 4 * GB;

	/* the image, then what it wants after it, in one run */
	for ( i = 0; i < eh->e_phnum; i++ ) {
		if ( ph[i].p_type == PT_LOAD && ph[i].p_paddr == lo && ph[i].p_filesz >= sizeof(T_KHDR) ) break;
	}
	if ( i == eh->e_phnum ) stop("tessronos.elf has no header", 0);
	kh = (T_KHDR *)( elf + ph[i].p_offset );
	if ( kh->magic != KHDR_MAGIC ) stop("tessronos.elf has no TessronOS header", 0);
	heap = kh->heap + kh->db_per_page * ( top / EFI_PAGE_SIZE );
	heap = ( heap + MB2 - 1 ) & ~( MB2 - 1 );
	kimg = alloc_pages(( span + heap ) / EFI_PAGE_SIZE, MEM_TESSRON);
	kpa = (UINT64)kimg;
	for ( i = 0; i < eh->e_phnum; i++ ) {
		if ( ph[i].p_type != PT_LOAD ) continue;
		memcpy(kimg + ( ph[i].p_paddr - lo ), elf + ph[i].p_offset, ph[i].p_filesz);
	}
	bi->kimg_pa = kpa;
	bi->kimg_va = kva;
	bi->kimg_size = span;
	bi->heap_pa = kpa + span;
	bi->heap_size = heap;
	bi->ram_top = top;
	bi->dmap_size = dmap;
	bi->flags = gb ? BI_F_1GPAGE : 0;
	say("TessronOS loader: kernel ");
	say_hex(kva);
	say(" at ");
	say_hex(kpa);
	say(", ");
	say_dec(span >> 10);
	say(" KB + ");
	say_dec(heap >> 20);
	say(" MB; RAM to ");
	say_hex(top);
	say("\n");

	setup_screen(bi);
	find_tables(bi);
	get_seed(bi);

	/* four pages below 1MB for the other processors to start in: the code, and its page tables */
	s = BS->AllocatePages(AllocateMaxAddress, MEM_TESSRON, 4, &tramp);
	if ( EFI_ERROR(s) || tramp < 0x1000 ) stop("no pages below 1MB", s);
	bi->tramp_pa = tramp;

	/*
	 * The page tables: every kernel entry of the PML4 with a table under
	 * it from the start, so that a process space can share them by
	 * copying the upper half; the linear map and the device window in
	 * large pages; the kernel image page by page, as its segments say;
	 * and the identity map of RAM in the lower half, which only the jump
	 * to the kernel uses
	 */
	pt_cap = 1 + 256 + 2 * ( dmap / GB ) * ( gb ? 0 : 1 ) + ( dmap / ( 512 * GB ) + 1 ) + 8
	       + ( span / MB2 + 2 ) + 4;
	pt_pool = alloc_pages(pt_cap, MEM_TESSRON);
	pt_used = 0;
	pml4 = pt_new();
	for ( i = 256; i < 512; i++ ) {
		pml4[i] = (UINT64)pt_new() | PG_P | PG_RW;
	}
	map_large(pml4, KVA_BASE, 0, dmap, PG_P | PG_RW | PG_A | PG_D | PG_G | PG_NX, gb);
	map_large(pml4, DEV_VA_BASE, 0, 4 * GB, PG_P | PG_RW | PG_A | PG_D | PG_G | PG_NX | PG_PCD | PG_PWT, 0);
	for ( i = 0; i < eh->e_phnum; i++ ) {
		UINT64	va, size, attr = PG_P | PG_A | PG_G;

		if ( ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0 || ph[i].p_vaddr < KIMG_VA_BASE ) continue;
		va = ph[i].p_vaddr & ~(UINT64)( EFI_PAGE_SIZE - 1 );
		size = ( ( ph[i].p_vaddr + ph[i].p_memsz + EFI_PAGE_SIZE - 1 ) & ~(UINT64)( EFI_PAGE_SIZE - 1 ) ) - va;
		if ( ph[i].p_flags & PF_W ) attr |= PG_RW | PG_D;
		if ( ( ph[i].p_flags & PF_X ) == 0 ) attr |= PG_NX;
		map_pages(pml4, va, kpa + ( ( ph[i].p_paddr & ~(UINT64)( EFI_PAGE_SIZE - 1 ) ) - lo ), size, attr);
	}
	map_large(pml4, 0, 0, dmap, PG_P | PG_RW | PG_A | PG_D, gb);
	bi->pml4_pa = (UINT64)pml4;

	/* the boot services left: nothing allocates after the map is taken */
	get_map();
	s = BS->ExitBootServices(image, mmap_key);
	if ( EFI_ERROR(s) ) {
		get_map();
		s = BS->ExitBootServices(image, mmap_key);
		if ( EFI_ERROR(s) ) stop("cannot leave the boot services", s);
	}
	booted_out = 1;
	convert_map(bi);
	say("TessronOS loader: to the kernel\n");

	__asm__ volatile ( "cli" );
	wrmsr(MSR_PAT, PAT_VALUE);
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);
	__asm__ volatile (
		"mov	%0, %%cr3\n\t"
		"mov	%1, %%rdi\n\t"
		"jmp	*%2"
		:: "r"((UINT64)pml4), "r"((UINT64)bi), "r"(eh->e_entry) : "rdi", "memory");
	return EFI_LOAD_ERROR;
}
