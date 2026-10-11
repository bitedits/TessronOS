/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	dlfcn.h
 *	Loading shared objects at run time, which a TessronOS process does
 *	not do: OpenGL ES and its drivers are linked into the program
 *	(docs/tessronos/13-gpu.md 13.6.2). lib/libgl/gl_os.c answers that
 *	nothing can be opened and no symbol found, and the callers in Mesa
 *	fall back on what is linked in.
 */

#ifndef TS_SHIM_DLFCN_H
#define TS_SHIM_DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY	0x0001
#define RTLD_NOW	0x0002
#define RTLD_LOCAL	0x0000
#define RTLD_GLOBAL	0x0100
#define RTLD_NOLOAD	0x0004
#define RTLD_NODELETE	0x1000
#define RTLD_DEFAULT	((void *)0)

typedef struct {
	const char	*dli_fname;
	void		*dli_fbase;
	const char	*dli_sname;
	void		*dli_saddr;
} Dl_info;

void	*dlopen( const char *file, int mode );
void	*dlsym( void *handle, const char *name );
int	dlclose( void *handle );
char	*dlerror( void );
int	dladdr( const void *addr, Dl_info *info );

#ifdef __cplusplus
}
#endif

#endif /* TS_SHIM_DLFCN_H */
