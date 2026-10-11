/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	locale.h
 *	newlib's, with the C locale as an object BSD programs name
 *	LC_C_LOCALE (design 5.8.3)
 */

#ifndef __POSIX_LOCALE_H__
#define __POSIX_LOCALE_H__

#include_next <locale.h>

#ifdef __cplusplus
extern "C" {
#endif

locale_t	__psx_c_locale( void );

#ifdef __cplusplus
}
#endif

#ifndef LC_C_LOCALE
#define LC_C_LOCALE	__psx_c_locale()
#endif
#ifndef LC_GLOBAL_LOCALE
#define LC_GLOBAL_LOCALE	((locale_t)-1)
#endif

#endif /* __POSIX_LOCALE_H__ */
