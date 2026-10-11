/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	fcntl.h
 *	newlib's <fcntl.h> and the sealing commands of a memory descriptor
 *	(memfd_create), which Mesa asks for on the descriptors it makes.
 *	lib/libcxxrt takes the commands it does not know as done, and a
 *	memory descriptor of a process does not change size unless the
 *	process asks, so sealing it changes nothing.
 */

#ifndef TS_SHIM_FCNTL_H
#define TS_SHIM_FCNTL_H

#include_next <fcntl.h>

#ifndef F_ADD_SEALS
#define F_ADD_SEALS	1033
#define F_GET_SEALS	1034
#define F_SEAL_SEAL	0x0001
#define F_SEAL_SHRINK	0x0002
#define F_SEAL_GROW	0x0004
#define F_SEAL_WRITE	0x0008
#endif

#endif /* TS_SHIM_FCNTL_H */
