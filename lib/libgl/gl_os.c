/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	gl_os.c
 *	The calls of the C library beyond newlib and lib/libcxxrt that Mesa
 *	makes, answered for a TessronOS process (docs/tessronos/13-gpu.md
 *	13.6.2):
 *
 *	- Shared objects are not loaded at run time: OpenGL ES and its
 *	  drivers are linked into the program, so dlopen finds nothing and
 *	  Mesa uses what is linked in.
 *	- The system log is the console.
 *	- sync has nothing to wait for.
 *	- zlib's inflating is only reached by the V3D control list decoder,
 *	  to unpack the packet descriptions it prints control lists with for
 *	  debugging; it answers that the data cannot be unpacked.
 *
 * Every one is weak: a program that has its own (the browser, whose
 * dlopen finds the EGL linked into it, and whose zlib is the engine's)
 * keeps it.
 */

#include <config.h>		/* first: the core's types as wide as the kernel's */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <tk/typedef.h>
#include <ts/uapp.h>
#include "dlfcn.h"
#include "syslog.h"
#include "zlib.h"

#define WEAK	__attribute__((weak))

/* ---------------------------------------------------------------- dlfcn */

WEAK void *dlopen( const char *file, int mode )
{
	(void)file;
	(void)mode;
	return NULL;
}

WEAK void *dlsym( void *handle, const char *name )
{
	(void)handle;
	(void)name;
	return NULL;
}

WEAK int dlclose( void *handle )
{
	(void)handle;
	return 0;
}

WEAK char *dlerror( void )
{
	static char msg[] = "no shared objects in a TessronOS process";

	return msg;
}

WEAK int dladdr( const void *addr, Dl_info *info )
{
	(void)addr;
	(void)info;
	return 0;
}

/* ---------------------------------------------------------------- syslog */

static const char *log_ident = "mesa";

WEAK void openlog( const char *ident, int option, int facility )
{
	(void)option;
	(void)facility;
	if ( ident != NULL ) {
		log_ident = ident;
	}
}

WEAK void syslog( int priority, const char *fmt, ... )
{
	char	buf[512];
	int	n;
	va_list	ap;

	(void)priority;
	n = snprintf(buf, sizeof(buf), "%s: ", log_ident);
	if ( n < 0 || n >= (int)sizeof(buf) - 2 ) {
		n = 0;
	}
	va_start(ap, fmt);
	(void)vsnprintf(buf + n, sizeof(buf) - (size_t)n - 1, fmt, ap);
	va_end(ap);
	n = (int)strlen(buf);
	if ( n == 0 || buf[n - 1] != '\n' ) {
		buf[n] = '\n';
		buf[n + 1] = '\0';
	}
	(void)tm_putstring((CONST UB *)buf);
}

WEAK void closelog( void )
{
}

/* ---------------------------------------------------------------- files */

/*
 * Everything written out, which the driver debugger asks for before it
 * may stop the process: the file layer has nothing kept back from the
 * volumes a process writes to.
 */
WEAK void sync( void )
{
}

/* ---------------------------------------------------------------- zlib */

WEAK int inflateInit( z_stream *strm )
{
	(void)strm;
	return Z_MEM_ERROR;
}

WEAK int inflate( z_stream *strm, int flush )
{
	(void)strm;
	(void)flush;
	return Z_DATA_ERROR;
}

WEAK int inflateEnd( z_stream *strm )
{
	(void)strm;
	return Z_OK;
}
