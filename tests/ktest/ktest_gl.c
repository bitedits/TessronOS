/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_gl.c
 *	The GPU for processes and OpenGL ES (docs/tessronos/13-gpu.md 13.5,
 *	13.6), through two programs started as processes:
 *
 *	/boot/GPPROG.ELF (tests/uprog/gpprog.c, always built) reaches SVC
 *	class 10 itself: its contexts and BOs, their mappings and names,
 *	jobs on a backend of the test's own, and BOs it leaves held when it
 *	ends, which the kernel lets go of. The backend gives each context
 *	an address space of its own (a V3D page table, as the board's does):
 *	the test looks, as each job is started, at what the job's table has
 *	at the addresses the job names -- the context's own pages, never
 *	another context's, never a BO of the kernel's.
 *
 *	/boot/GLPROG.ELF (tests/uprog/glprog.c, made with make GL=1) draws
 *	with Mesa -- softpipe, as the machine has no GPU -- and reads what it
 *	drew back: a triangle, the depth test, textures with mipmaps, GLSL
 *	ES 3.00, a framebuffer object with no surface, a window of its own;
 *	and times frames. Against the DRM shim's pretended V3D, it checks
 *	that the platform chooses the driver v3d and makes its screen.
 *	Skipped when the program is not on the volume.
 */

#include <sys/machine.h>
#include "kernel.h"
#include "ktest.h"
#include <ts/proc.h>
#include <ts/fs.h>
#include <ts/gpu.h>
#include "../../device/gpu/gpudev.h"
#include "../../device/gpu/v3d/v3d.h"

IMPORT UD knl_pf_free_count( INT zone );
#if TS_NETSTACK_NETBSD
IMPORT UD knl_rump_pages( void );
#endif

#define GP_PROG		"/boot/GPPROG.ELF"
#define GL_PROG		"/boot/GLPROG.ELF"
#define GP_TMO		60000		/* ms */
#define GL_TMO		600000		/* ms: softpipe under TCG */

LOCAL UD pages_free( void )
{
#if TS_NETSTACK_NETBSD
	return knl_pf_free_count(-1) + knl_rump_pages();
#else
	return knl_pf_free_count(-1);
#endif
}

LOCAL BOOL present( CONST char *path )
{
	T_FSTAT	st;

	if ( fs_stat(path, &st) < EX_OK ) return FALSE;
	tm_printf((UB*)"  %s: %d bytes\n", path, (INT)st.size);
	return TRUE;
}

/* The program run to its end with the argument; its exit code, or -1 */
LOCAL INT run( CONST char *prog, CONST char *mode, TMO tmo )
{
	T_CPRC	cprc;
	T_PSTS	psts;
	SZ	n = 0;
	ID	pid;
	ER	er;

	while ( mode[n] != '\0' ) n++;
	cprc.pri    = KT_PRI_HIGH;
	cprc.prcatr = 0;
	cprc.stksz  = 0;
	cprc.arg    = (void *)mode;
	cprc.argsz  = n;
	pid = ts_cre_prc(prog, &cprc);
	KT_ASSERT(pid > 0);
	if ( pid <= 0 ) return -1;
	psts.exitcd = -1;
	er = ts_wai_prc(pid, &psts, tmo);
	KT_ASSERT_ER(er, E_OK);
	if ( er < E_OK ) {
		(void)ts_ter_prc(pid, TS_ABORT_TERM);
		(void)ts_wai_prc(pid, &psts, 5000);
		return -1;
	}
	return psts.exitcd;
}

/* ---------------------------------------------------------------- gp_ */

/* Contexts, BOs, mappings and names; nothing left open after */
LOCAL void gl_gp_bo( void )
{
	INT	bos = -1;
	UD	before;
	INT	code;

	if ( !present(GP_PROG) ) KT_SKIP("no " GP_PROG);
	before = pages_free();
	code = run(GP_PROG, "bo", GP_TMO);
	tm_printf((UB*)"  exit code %d\n", code);
	KT_ASSERT_EQ(code, 0);
	KT_ASSERT_EQ(knl_gpu_ctx_count(&bos), 0);
	KT_ASSERT_EQ(bos, 0);
	KT_ASSERT_EQ(pages_free(), before);
}

/*
 * A backend that runs every job at once, gives the kernel's BOs addresses
 * one after another, and gives each context a page table of its own
 * (v3d_mmu.c, 64MB of addresses)
 */
#define JB_AS_VA	( 64UL * 1024 * 1024 )
#define JB_SEEN		8

typedef struct {
	T_V3DMMU	mmu;
} JBAS;

LOCAL UW	jb_next;
LOCAL INT	jb_kicked;
LOCAL INT	jb_as_live;
LOCAL struct {
	BOOL	own;		/* kicked in a space of its own */
	UW	bcl;		/* the entry at its binner list's address */
	UW	rcl;		/* and at its renderer list's */
} jb_seen[JB_SEEN];

LOCAL ER jb_info( void *priv, T_GPUINFO *info )
{
	info->kind = GPU_KIND_TEST;
	info->feature = GPU_FEAT_CL | GPU_FEAT_CACHE;
	info->va_size = 0x40000000;
	return E_OK;
}

LOCAL ER jb_bind( void *priv, T_GPUBO *bo, UD npages, UW *p_offset )
{
	*p_offset = jb_next;
	jb_next += (UW)( npages << 12 );
	return E_OK;
}

LOCAL void jb_unbind( void *priv, UW offset, UD npages )
{
}

LOCAL ER jb_kick( void *priv, CONST T_GPUJOB *job, void *as )
{
	JBAS	*a = (JBAS *)as;

	if ( jb_kicked < JB_SEEN ) {
		jb_seen[jb_kicked].own = ( a != NULL );
		jb_seen[jb_kicked].bcl = ( a != NULL ) ? v3d_mmu_lookup(&a->mmu, job->u.cl.bcl_start) : 0;
		jb_seen[jb_kicked].rcl = ( a != NULL ) ? v3d_mmu_lookup(&a->mmu, job->u.cl.rcl_start) : 0;
	}
	jb_kicked++;
	knl_gpu_job_end(E_OK);
	return E_OK;
}

LOCAL ER jb_as_create( void *priv, void **p_as )
{
	JBAS	*a = (JBAS *)Kmalloc(sizeof(JBAS));
	ER	er;

	if ( a == NULL ) return E_NOMEM;
	knl_memset(a, 0, sizeof(*a));
	er = v3d_mmu_init(&a->mmu, JB_AS_VA);
	if ( er < E_OK ) {
		Kfree(a);
		return er;
	}
	jb_as_live++;
	*p_as = a;
	return E_OK;
}

LOCAL void jb_as_destroy( void *priv, void *as )
{
	v3d_mmu_fini(&((JBAS *)as)->mmu);
	Kfree(as);
	jb_as_live--;
}

LOCAL ER jb_as_bind( void *priv, void *as, T_GPUBO *bo, UD npages, UW *p_va )
{
	JBAS	*a = (JBAS *)as;
	ER	er = v3d_mmu_alloc(&a->mmu, npages, 1, p_va);

	if ( er < E_OK ) return er;
	v3d_mmu_insert(&a->mmu, *p_va, knl_gpu_bo_pages(bo), npages, TRUE);
	return E_OK;
}

LOCAL void jb_as_unbind( void *priv, void *as, UW va, UD npages )
{
	JBAS	*a = (JBAS *)as;

	v3d_mmu_remove(&a->mmu, va, npages);
	v3d_mmu_free(&a->mmu, va, npages);
}

LOCAL void jb_reset( void *priv )
{
}

LOCAL CONST T_GPUOPS	jb_ops = {
	GPU_KIND_TEST, GPU_FEAT_CL | GPU_FEAT_CACHE, 0,
	jb_info, jb_bind, jb_unbind, jb_kick, jb_reset,
	jb_as_create, jb_as_destroy, jb_as_bind, jb_as_unbind
};

/* Hex digits of v at the end of the string */
LOCAL void put_hex( char *d, UW v )
{
	INT	i;

	for ( i = 7; i >= 0; i-- ) {
		d[i] = "0123456789abcdef"[v & 0xF];
		v >>= 4;
	}
	d[8] = '\0';
}

/*
 * Jobs from a process: fences, waits, numbers not its own refused; each
 * context's jobs in its own space, which holds its BOs and nothing of
 * the kernel's or of the other context's
 */
LOCAL void gl_gp_job( void )
{
	T_GPUBO		*kbo;
	T_GPUBOINFO	ki;
	char		arg[16] = "job ";
	INT		code, bos = -1, i;
	ER		er;

	if ( !present(GP_PROG) ) KT_SKIP("no " GP_PROG);
	jb_next = 0x100000;
	jb_kicked = 0;
	jb_as_live = 0;
	knl_memset(jb_seen, 0, sizeof(jb_seen));
	er = knl_gpu_register(&jb_ops, NULL);
	if ( er == E_OBJ ) KT_SKIP("a GPU is registered already");
	KT_ASSERT_ER(er, E_OK);

	/* a BO of the kernel's, whose address the program names in its jobs */
	kbo = knl_gpu_bo_create(4096, 0, &er);
	KT_ASSERT(kbo != NULL);
	if ( kbo == NULL ) goto out;
	knl_gpu_bo_ref(kbo, &ki);
	put_hex(arg + 4, ki.offset);
	code = run(GP_PROG, arg, GP_TMO);
	tm_printf((UB*)"  exit code %d, %d jobs run, the kernel's BO at %08x\n", code, jb_kicked, ki.offset);
	for ( i = 0; i < jb_kicked && i < JB_SEEN; i++ ) {
		tm_printf((UB*)"  job %d: own space %d, entries %08x at its lists, %08x at the kernel's BO\n",
			  i, jb_seen[i].own, jb_seen[i].bcl, jb_seen[i].rcl);
	}
	KT_ASSERT_EQ(code, 0);
	KT_ASSERT_EQ(jb_kicked, 3);			/* the jobs of control lists */
	for ( i = 0; i < 3; i++ ) {
		KT_ASSERT(jb_seen[i].own);		/* never in the kernel's space */
		KT_ASSERT(jb_seen[i].bcl != 0);		/* its own BO is there */
	}
	/* the two contexts' BOs at the same address are different pages */
	KT_ASSERT(jb_seen[1].bcl != jb_seen[2].bcl);
	/* the kernel's BO is in neither */
	KT_ASSERT_EQ(jb_seen[1].rcl, 0);
	KT_ASSERT_EQ(jb_seen[2].rcl, 0);
	knl_gpu_bo_put(kbo);
out:
	KT_ASSERT_EQ(knl_gpu_ctx_count(&bos), 0);
	KT_ASSERT_EQ(jb_as_live, 0);			/* the spaces went with the contexts */
	KT_ASSERT_ER(knl_gpu_unregister(), E_OK);
}

/* A process that ends holding BOs in two contexts: all of it let go of */
LOCAL void gl_gp_hold( void )
{
	INT	code, bos = -1;
	UD	before;

	if ( !present(GP_PROG) ) KT_SKIP("no " GP_PROG);
	before = pages_free();
	code = run(GP_PROG, "hold", GP_TMO);
	KT_ASSERT_EQ(code, 0);
	KT_ASSERT_EQ(knl_gpu_ctx_count(&bos), 0);
	KT_ASSERT_EQ(bos, 0);
	tm_printf((UB*)"  free pages %d -> %d\n", (INT)before, (INT)pages_free());
	KT_ASSERT_EQ(pages_free(), before);
}

/* ---------------------------------------------------------------- OpenGL ES */

LOCAL BOOL	have_gl;

LOCAL void gl_present( void )
{
	have_gl = present(GL_PROG);
	if ( !have_gl ) KT_SKIP("no " GL_PROG " (make GL=1)");
}

LOCAL void gl_mode( CONST char *mode )
{
	INT	code;

	if ( !have_gl ) KT_SKIP("no " GL_PROG);
	code = run(GL_PROG, mode, GL_TMO);
	KT_ASSERT_EQ(code, 0);
}

LOCAL void gl_info( void )	{ gl_mode("info"); }
LOCAL void gl_depth( void )	{ gl_mode("depth"); }
LOCAL void gl_tex( void )	{ gl_mode("tex"); }
LOCAL void gl_glsl( void )	{ gl_mode("glsl"); }
LOCAL void gl_fbo( void )	{ gl_mode("fbo"); }
LOCAL void gl_speed( void )	{ gl_mode("speed"); }

/* The driver v3d chosen and its screen made, against the DRM shim's pretended V3D */
LOCAL void gl_v3dpath( void )	{ gl_mode("v3dpath"); }

/* A triangle; and the pages of the run all come back */
LOCAL void gl_tri( void )
{
	UD	before;

	if ( !have_gl ) KT_SKIP("no " GL_PROG);
	before = pages_free();
	gl_mode("tri");
	tm_printf((UB*)"  free pages %d -> %d\n", (INT)before, (INT)pages_free());
	KT_ASSERT_EQ(pages_free(), before);
}

/* A window of its own: the program reads its pixels back */
LOCAL void gl_win( void )
{
	if ( !kt_screen() ) KT_SKIP(KT_NO_SCREEN);
	gl_mode("win");
}

EXPORT void ktest_gl( void )
{
	KT_RUN(gl_gp_bo);
	KT_RUN(gl_gp_job);
	KT_RUN(gl_gp_hold);
	KT_RUN(gl_present);
	KT_RUN(gl_info);
	KT_RUN(gl_tri);
	KT_RUN(gl_depth);
	KT_RUN(gl_tex);
	KT_RUN(gl_glsl);
	KT_RUN(gl_fbo);
	KT_RUN(gl_win);
	KT_RUN(gl_v3dpath);
	KT_RUN(gl_speed);
}
