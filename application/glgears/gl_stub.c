/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	gl_stub.c
 *	歯車, when the system was built without OpenGL ES (docs/tessronos/
 *	13-gpu.md 13.6): the program is linked with Mesa, which is built
 *	outside the repository by tools/mesa/build.sh, with make GL=1. This
 *	program stands in the program object's record 1 otherwise and only
 *	says so, on the message line and the console.
 */

#include <tk/typedef.h>
#include <ts/uapp.h>
#include <ts/wm.h>

int main( void )
{
	(void)wm_msg_put("歯車はこのシステムに入っていません(make GL=1 で作ります)");
	(void)tm_putstring((CONST UB *)"glgears: not built in this system (make GL=1)\n");
	return 0;
}
