/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	glapp.h
 *	OpenGL ES in a window of one's own (docs/tessronos/13-gpu.md 13.6.3)
 *
 *	A program linked with OpenGL ES (make GL=1: the static library of
 *	tools/mesa/build.sh) includes <EGL/egl.h> and <GLES2/gl2.h> or
 *	<GLES3/gl3.h> with EGL_NO_PLATFORM_SPECIFIC_TYPES defined, and this.
 *	The display is that of the platform tessronos:
 *
 *		eglGetPlatformDisplay(EGL_PLATFORM_TESSRONOS, NULL, NULL)
 *
 *	or eglGetDisplay(EGL_DEFAULT_DISPLAY), which is the same. A window
 *	surface is made from the window's key, opened to write (OB_OP_WRITE),
 *	in a TS_EGLWINDOW given as the native window. Its pixels are the
 *	surface of the window's work area (wm_obj_surf): eglSwapBuffers puts
 *	the frame there and shows the rectangles that changed (wm_obj_put),
 *	or only those named to eglSwapBuffersWithDamageKHR. After the window
 *	changes size the surface takes the new size at the next frame. The
 *	TS_EGLWINDOW must stay where it is while the surface is used.
 *
 *	A pbuffer surface (eglCreatePbufferSurface) and a context with no
 *	surface (EGL_KHR_surfaceless_context, with framebuffer objects) draw
 *	without a window; glReadPixels reads what they drew.
 *
 *	Without a GPU the drawing is done by the processor (softpipe); a
 *	program sees no difference but the speed.
 */

#ifndef __TS_GLAPP_H__
#define __TS_GLAPP_H__

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The platform of eglGetPlatformDisplay (a provisional value of TessronOS's) */
#define EGL_PLATFORM_TESSRONOS	0x3FD0

/* The native window of the platform: a window of the window system */
typedef struct {
	ID	key;		/* the window object, opened with OB_OP_WRITE */
} TS_EGLWINDOW;

#ifdef __cplusplus
}
#endif

#endif /* __TS_GLAPP_H__ */
