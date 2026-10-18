/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	efi.h
 *	The parts of the UEFI interfaces the loader uses (UEFI 2.x):
 *	the system table, the boot services, the simple text output, the
 *	loaded image, the simple file system and its file, the graphics
 *	output and the random number protocol. Functions are called with
 *	the Microsoft x64 convention (EFIAPI), the default of this
 *	compiler; it is said once more on every pointer.
 */

#ifndef _TS_UEFI_EFI_H_
#define _TS_UEFI_EFI_H_

typedef unsigned char		UINT8;
typedef unsigned short		UINT16;
typedef unsigned int		UINT32;
typedef unsigned long long	UINT64;
typedef long long		INT64;
typedef unsigned long long	UINTN;
typedef unsigned short		CHAR16;
typedef unsigned char		BOOLEAN;
typedef void			*EFI_HANDLE;
typedef void			*EFI_EVENT;
typedef UINTN			EFI_STATUS;
typedef UINT64			EFI_PHYSICAL_ADDRESS;
typedef UINT64			EFI_VIRTUAL_ADDRESS;

#define EFIAPI			__attribute__((ms_abi))

#define EFI_ERROR_BIT		0x8000000000000000ull
#define EFI_ERROR(s)		( ( (s) & EFI_ERROR_BIT ) != 0 )
#define EFI_SUCCESS		0
#define EFI_LOAD_ERROR		( EFI_ERROR_BIT | 1 )
#define EFI_INVALID_PARAMETER	( EFI_ERROR_BIT | 2 )
#define EFI_UNSUPPORTED		( EFI_ERROR_BIT | 3 )
#define EFI_BUFFER_TOO_SMALL	( EFI_ERROR_BIT | 5 )
#define EFI_NOT_FOUND		( EFI_ERROR_BIT | 14 )

typedef struct {
	UINT32	d1;
	UINT16	d2;
	UINT16	d3;
	UINT8	d4[8];
} EFI_GUID;

typedef struct {
	UINT64	Signature;
	UINT32	Revision;
	UINT32	HeaderSize;
	UINT32	CRC32;
	UINT32	Reserved;
} EFI_TABLE_HEADER;

/* Simple text output */
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
	EFI_STATUS (EFIAPI *Reset)( struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, BOOLEAN ext );
	EFI_STATUS (EFIAPI *OutputString)( struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *s );
	void	*TestString, *QueryMode, *SetMode, *SetAttribute;
	EFI_STATUS (EFIAPI *ClearScreen)( struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self );
	void	*SetCursorPosition, *EnableCursor, *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Memory */
typedef enum {
	AllocateAnyPages,
	AllocateMaxAddress,
	AllocateAddress,
	MaxAllocateType
} EFI_ALLOCATE_TYPE;

#define EfiReservedMemoryType		0
#define EfiLoaderCode			1
#define EfiLoaderData			2
#define EfiBootServicesCode		3
#define EfiBootServicesData		4
#define EfiRuntimeServicesCode		5
#define EfiRuntimeServicesData		6
#define EfiConventionalMemory		7
#define EfiUnusableMemory		8
#define EfiACPIReclaimMemory		9
#define EfiACPIMemoryNVS		10
#define EfiMemoryMappedIO		11
#define EfiMemoryMappedIOPortSpace	12
#define EfiPalCode			13
#define EfiPersistentMemory		14
#define EfiUnacceptedMemoryType		15

typedef struct {
	UINT32			Type;
	UINT32			Pad;
	EFI_PHYSICAL_ADDRESS	PhysicalStart;
	EFI_VIRTUAL_ADDRESS	VirtualStart;
	UINT64			NumberOfPages;
	UINT64			Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EFI_PAGE_SIZE		4096

typedef enum {
	AllHandles,
	ByRegisterNotify,
	ByProtocol
} EFI_LOCATE_SEARCH_TYPE;

/* Boot services: every entry up to LocateProtocol, in the table's order */
typedef struct {
	EFI_TABLE_HEADER	Hdr;
	void	*RaiseTPL;
	void	*RestoreTPL;
	EFI_STATUS (EFIAPI *AllocatePages)( EFI_ALLOCATE_TYPE type, UINT32 memtype, UINTN pages,
					    EFI_PHYSICAL_ADDRESS *addr );
	EFI_STATUS (EFIAPI *FreePages)( EFI_PHYSICAL_ADDRESS addr, UINTN pages );
	EFI_STATUS (EFIAPI *GetMemoryMap)( UINTN *size, EFI_MEMORY_DESCRIPTOR *map, UINTN *key,
					   UINTN *dsize, UINT32 *dver );
	EFI_STATUS (EFIAPI *AllocatePool)( UINT32 memtype, UINTN size, void **buf );
	EFI_STATUS (EFIAPI *FreePool)( void *buf );
	void	*CreateEvent;
	void	*SetTimer;
	void	*WaitForEvent;
	void	*SignalEvent;
	void	*CloseEvent;
	void	*CheckEvent;
	void	*InstallProtocolInterface;
	void	*ReinstallProtocolInterface;
	void	*UninstallProtocolInterface;
	EFI_STATUS (EFIAPI *HandleProtocol)( EFI_HANDLE h, EFI_GUID *proto, void **iface );
	void	*Reserved;
	void	*RegisterProtocolNotify;
	void	*LocateHandle;
	void	*LocateDevicePath;
	void	*InstallConfigurationTable;
	void	*LoadImage;
	void	*StartImage;
	void	*Exit;
	void	*UnloadImage;
	EFI_STATUS (EFIAPI *ExitBootServices)( EFI_HANDLE image, UINTN key );
	void	*GetNextMonotonicCount;
	EFI_STATUS (EFIAPI *Stall)( UINTN usec );
	EFI_STATUS (EFIAPI *SetWatchdogTimer)( UINTN timeout, UINT64 code, UINTN size, CHAR16 *data );
	void	*ConnectController;
	void	*DisconnectController;
	void	*OpenProtocol;
	void	*CloseProtocol;
	void	*OpenProtocolInformation;
	void	*ProtocolsPerHandle;
	void	*LocateHandleBuffer;
	EFI_STATUS (EFIAPI *LocateProtocol)( EFI_GUID *proto, void *reg, void **iface );
} EFI_BOOT_SERVICES;

typedef struct {
	EFI_GUID	VendorGuid;
	void		*VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
	EFI_TABLE_HEADER		Hdr;
	CHAR16				*FirmwareVendor;
	UINT32				FirmwareRevision;
	EFI_HANDLE			ConsoleInHandle;
	void				*ConIn;
	EFI_HANDLE			ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL	*ConOut;
	EFI_HANDLE			StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL	*StdErr;
	void				*RuntimeServices;
	EFI_BOOT_SERVICES		*BootServices;
	UINTN				NumberOfTableEntries;
	EFI_CONFIGURATION_TABLE		*ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* Loaded image */
typedef struct {
	UINT32			Revision;
	EFI_HANDLE		ParentHandle;
	EFI_SYSTEM_TABLE	*SystemTable;
	EFI_HANDLE		DeviceHandle;
	void			*FilePath;
	void			*Reserved;
	UINT32			LoadOptionsSize;
	void			*LoadOptions;
	void			*ImageBase;
	UINT64			ImageSize;
	UINT32			ImageCodeType;
	UINT32			ImageDataType;
	void			*Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* Files */
#define EFI_FILE_MODE_READ	0x0000000000000001ull

typedef struct EFI_FILE_PROTOCOL {
	UINT64	Revision;
	EFI_STATUS (EFIAPI *Open)( struct EFI_FILE_PROTOCOL *self, struct EFI_FILE_PROTOCOL **out,
				   CHAR16 *name, UINT64 mode, UINT64 attr );
	EFI_STATUS (EFIAPI *Close)( struct EFI_FILE_PROTOCOL *self );
	void	*Delete;
	EFI_STATUS (EFIAPI *Read)( struct EFI_FILE_PROTOCOL *self, UINTN *size, void *buf );
	void	*Write;
	EFI_STATUS (EFIAPI *GetPosition)( struct EFI_FILE_PROTOCOL *self, UINT64 *pos );
	EFI_STATUS (EFIAPI *SetPosition)( struct EFI_FILE_PROTOCOL *self, UINT64 pos );
	void	*GetInfo;
	void	*SetInfo;
	void	*Flush;
} EFI_FILE_PROTOCOL;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
	UINT64	Revision;
	EFI_STATUS (EFIAPI *OpenVolume)( struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *self,
					 EFI_FILE_PROTOCOL **root );
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* Graphics output */
typedef enum {
	PixelRedGreenBlueReserved8BitPerColor,
	PixelBlueGreenRedReserved8BitPerColor,
	PixelBitMask,
	PixelBltOnly,
	PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
	UINT32	RedMask;
	UINT32	GreenMask;
	UINT32	BlueMask;
	UINT32	ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
	UINT32				Version;
	UINT32				HorizontalResolution;
	UINT32				VerticalResolution;
	EFI_GRAPHICS_PIXEL_FORMAT	PixelFormat;
	EFI_PIXEL_BITMASK		PixelInformation;
	UINT32				PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
	UINT32					MaxMode;
	UINT32					Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION	*Info;
	UINTN					SizeOfInfo;
	EFI_PHYSICAL_ADDRESS			FrameBufferBase;
	UINTN					FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
	EFI_STATUS (EFIAPI *QueryMode)( struct EFI_GRAPHICS_OUTPUT_PROTOCOL *self, UINT32 mode,
					UINTN *size, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info );
	EFI_STATUS (EFIAPI *SetMode)( struct EFI_GRAPHICS_OUTPUT_PROTOCOL *self, UINT32 mode );
	void					*Blt;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE	*Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* Random numbers */
typedef struct EFI_RNG_PROTOCOL {
	void	*GetInfo;
	EFI_STATUS (EFIAPI *GetRNG)( struct EFI_RNG_PROTOCOL *self, EFI_GUID *alg, UINTN len, UINT8 *buf );
} EFI_RNG_PROTOCOL;

/* GUIDs */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
	{ 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
	{ 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
	{ 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }
#define EFI_RNG_PROTOCOL_GUID \
	{ 0x3152bca5, 0xeade, 0x433d, { 0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44 } }
#define EFI_ACPI_20_TABLE_GUID \
	{ 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }
#define EFI_ACPI_10_TABLE_GUID \
	{ 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
#define EFI_SMBIOS_TABLE_GUID \
	{ 0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
#define EFI_SMBIOS3_TABLE_GUID \
	{ 0xf2fd1544, 0x9794, 0x4a2c, { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } }

#endif /* _TS_UEFI_EFI_H_ */
