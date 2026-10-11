/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	glgears.c
 *	歯車: three gears turning in a window, drawn with OpenGL ES 2 and
 *	GLSL shaders (docs/tessronos/13-gpu.md 13.6). A program running as a
 *	process, made with make GL=1; the record 1 of its program object.
 *
 *	The window is a window object of its own; its key, opened to write,
 *	is the EGL native window of the platform tessronos (include/ts/
 *	glapp.h). Each frame is drawn into the window's surface by
 *	eglSwapBuffers and the rectangles that changed are shown. The gears
 *	are solids of flat faces lit in the vertex shader. The window may be
 *	made larger or smaller: the frame follows its work area.
 *
 *	The frames drawn in each five seconds are written on the console,
 *	with the time a frame took to draw (the calls that only queue the
 *	GPU's work) and in eglSwapBuffers (the GPU finishing it, the copy
 *	into the window's pixels, and the wait for the screen to be built
 *	with it), on average and at most. A start-up argument "frames=N"
 *	ends the program after N frames.
 */

#include <config.h>		/* first: the core's types as wide as the kernel's */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/uapp.h>
#include <ts/glapp.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

SZ	ts_get_arg( void *buf, SZ size );

#define WORK_W		320		/* the work area at first */
#define WORK_H		280
#define PI_F		3.14159265f

typedef struct {
	GLuint	vbo;
	GLsizei	count;			/* vertices, three to a triangle */
	GLfloat	colour[4];
	GLfloat	x, y;			/* where its axle is */
} GEAR;

static GEAR	gear[3];
static GLuint	prog;
static GLint	u_mvp, u_normal, u_colour;

static long now_ms( void )
{
	struct timespec	t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static long long now_us( void )
{
	struct timespec	t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}

/* ---------------------------------------------------------------- the solids */

typedef struct {
	GLfloat	*v;			/* x, y, z, nx, ny, nz each */
	int	n, max;
} MESH;

static void put( MESH *m, GLfloat x, GLfloat y, GLfloat z, GLfloat nx, GLfloat ny, GLfloat nz )
{
	GLfloat	*d;

	if ( m->n == m->max ) {
		m->max = m->max ? m->max * 2 : 1024;
		m->v = realloc(m->v, sizeof(GLfloat) * 6 * (size_t)m->max);
	}
	d = m->v + 6 * m->n++;
	d[0] = x; d[1] = y; d[2] = z;
	d[3] = nx; d[4] = ny; d[5] = nz;
}

/* A quad of four corners going round one way, as two triangles with one normal */
static void quad( MESH *m, const GLfloat *a, const GLfloat *b, const GLfloat *c, const GLfloat *d,
		  GLfloat nx, GLfloat ny, GLfloat nz )
{
	put(m, a[0], a[1], a[2], nx, ny, nz);
	put(m, b[0], b[1], b[2], nx, ny, nz);
	put(m, c[0], c[1], c[2], nx, ny, nz);
	put(m, a[0], a[1], a[2], nx, ny, nz);
	put(m, c[0], c[1], c[2], nx, ny, nz);
	put(m, d[0], d[1], d[2], nx, ny, nz);
}

/*
 * A gear: a ring from the hole's radius out to the teeth, 'width' thick.
 * Its outline goes round the teeth: four points to a tooth, two at the
 * root circle and two at the tip circle. The faces are the two sides of
 * the ring, the outline's edges between them, and the hole's wall.
 */
static void make_gear( GEAR *g, GLfloat hole, GLfloat radius, GLfloat width, int teeth, GLfloat depth )
{
	MESH	m = { NULL, 0, 0 };
	int	n = teeth * 4, i;
	GLfloat	root = radius - depth / 2.0f, tip = radius + depth / 2.0f, z = width / 2.0f;

	for ( i = 0; i < n; i++ ) {
		/* the outline from point i to point i + 1, and the hole beneath them */
		GLfloat	a0 = 2.0f * PI_F * i / n, a1 = 2.0f * PI_F * ( i + 1 ) / n;
		int	j = i % 4, j1 = ( i + 1 ) % 4;
		GLfloat	r0 = ( j == 1 || j == 2 ) ? tip : root;
		GLfloat	r1 = ( j1 == 1 || j1 == 2 ) ? tip : root;
		GLfloat	c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		GLfloat	o0f[3] = { r0 * c0, r0 * s0, z }, o1f[3] = { r1 * c1, r1 * s1, z };
		GLfloat	o0b[3] = { r0 * c0, r0 * s0, -z }, o1b[3] = { r1 * c1, r1 * s1, -z };
		GLfloat	h0f[3] = { hole * c0, hole * s0, z }, h1f[3] = { hole * c1, hole * s1, z };
		GLfloat	h0b[3] = { hole * c0, hole * s0, -z }, h1b[3] = { hole * c1, hole * s1, -z };
		GLfloat	ex = o1f[0] - o0f[0], ey = o1f[1] - o0f[1], el = sqrtf(ex * ex + ey * ey);

		quad(&m, h0f, o0f, o1f, h1f, 0.0f, 0.0f, 1.0f);		/* the front */
		quad(&m, h1b, o1b, o0b, h0b, 0.0f, 0.0f, -1.0f);	/* the back */
		quad(&m, o0f, o0b, o1b, o1f, ey / el, -ex / el, 0.0f);	/* the outline's edge */
		quad(&m, h1f, h1b, h0b, h0f, -( c0 + c1 ) / 2.0f, -( s0 + s1 ) / 2.0f, 0.0f);	/* the hole */
	}
	glGenBuffers(1, &g->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 6 * (size_t)m.n, m.v, GL_STATIC_DRAW);
	g->count = m.n;
	free(m.v);
}

/* ---------------------------------------------------------------- matrices */

static void mat_mul( GLfloat *r, const GLfloat *a, const GLfloat *b )
{
	GLfloat	t[16];
	int	i, j, k;

	for ( i = 0; i < 4; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			GLfloat s = 0.0f;

			for ( k = 0; k < 4; k++ ) s += a[k * 4 + j] * b[i * 4 + k];
			t[i * 4 + j] = s;
		}
	}
	memcpy(r, t, sizeof(t));
}

static void mat_ident( GLfloat *m )
{
	memset(m, 0, sizeof(GLfloat) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_translate( GLfloat *m, GLfloat x, GLfloat y, GLfloat z )
{
	GLfloat	t[16];

	mat_ident(t);
	t[12] = x;
	t[13] = y;
	t[14] = z;
	mat_mul(m, m, t);
}

/* about the axis (x, y, z), which is of length 1 */
static void mat_rotate( GLfloat *m, GLfloat deg, GLfloat x, GLfloat y, GLfloat z )
{
	GLfloat	r[16], a = deg * PI_F / 180.0f, c = cosf(a), s = sinf(a), k = 1.0f - c;

	mat_ident(r);
	r[0] = x * x * k + c;		r[4] = x * y * k - z * s;	r[8] = x * z * k + y * s;
	r[1] = y * x * k + z * s;	r[5] = y * y * k + c;		r[9] = y * z * k - x * s;
	r[2] = x * z * k - y * s;	r[6] = y * z * k + x * s;	r[10] = z * z * k + c;
	mat_mul(m, m, r);
}

static void mat_frustum( GLfloat *m, GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f )
{
	memset(m, 0, sizeof(GLfloat) * 16);
	m[0] = 2.0f * n / ( r - l );
	m[5] = 2.0f * n / ( t - b );
	m[8] = ( r + l ) / ( r - l );
	m[9] = ( t + b ) / ( t - b );
	m[10] = -( f + n ) / ( f - n );
	m[11] = -1.0f;
	m[14] = -2.0f * f * n / ( f - n );
}

/* ---------------------------------------------------------------- drawing */

static const char *vs_src =
	"attribute vec3 pos;\n"
	"attribute vec3 nrm;\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 normal_m;\n"
	"uniform vec4 colour;\n"
	"varying vec4 c;\n"
	"void main() {\n"
	"  vec3 n = normalize((normal_m * vec4(nrm, 0.0)).xyz);\n"
	"  vec3 l = normalize(vec3(5.0, 5.0, 10.0));\n"
	"  float d = max(dot(n, l), 0.0);\n"
	"  c = vec4(colour.rgb * (0.25 + 0.75 * d), colour.a);\n"
	"  gl_Position = mvp * vec4(pos, 1.0);\n"
	"}\n";

static const char *fs_src =
	"precision mediump float;\n"
	"varying vec4 c;\n"
	"void main() { gl_FragColor = c; }\n";

static GLuint compile( GLenum type, const char *src )
{
	GLuint	s = glCreateShader(type);
	GLint	ok = 0;
	char	log[512];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if ( !ok ) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("glgears: shader: %s\n", log);
	}
	return s;
}

static int setup( void )
{
	GLint	ok = 0;

	prog = glCreateProgram();
	glAttachShader(prog, compile(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs_src));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "nrm");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if ( !ok ) return 0;
	glUseProgram(prog);
	u_mvp = glGetUniformLocation(prog, "mvp");
	u_normal = glGetUniformLocation(prog, "normal_m");
	u_colour = glGetUniformLocation(prog, "colour");

	make_gear(&gear[0], 1.0f, 4.0f, 1.0f, 20, 0.7f);
	make_gear(&gear[1], 0.5f, 2.0f, 2.0f, 10, 0.7f);
	make_gear(&gear[2], 1.3f, 2.0f, 0.5f, 10, 0.7f);
	gear[0].x = -3.0f;	gear[0].y = -2.0f;
	gear[1].x = 3.1f;	gear[1].y = -2.0f;
	gear[2].x = -3.1f;	gear[2].y = 4.2f;
	memcpy(gear[0].colour, (GLfloat[4]){ 0.8f, 0.1f, 0.0f, 1.0f }, sizeof(gear[0].colour));
	memcpy(gear[1].colour, (GLfloat[4]){ 0.0f, 0.8f, 0.2f, 1.0f }, sizeof(gear[1].colour));
	memcpy(gear[2].colour, (GLfloat[4]){ 0.2f, 0.2f, 1.0f, 1.0f }, sizeof(gear[2].colour));

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	return 1;
}

static void draw( int w, int h, GLfloat angle )
{
	GLfloat	proj[16], view[16], m[16], mvp[16];
	GLfloat	aspect = (GLfloat)h / (GLfloat)w;
	GLfloat	turn[3] = { angle, -2.0f * angle - 9.0f, -2.0f * angle - 25.0f };
	int	i;

	glViewport(0, 0, w, h);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	mat_frustum(proj, -1.0f, 1.0f, -aspect, aspect, 5.0f, 60.0f);
	mat_ident(view);
	mat_translate(view, 0.0f, 0.0f, -40.0f);
	mat_rotate(view, 20.0f, 1.0f, 0.0f, 0.0f);
	mat_rotate(view, 30.0f, 0.0f, 1.0f, 0.0f);
	for ( i = 0; i < 3; i++ ) {
		memcpy(m, view, sizeof(m));
		mat_translate(m, gear[i].x, gear[i].y, 0.0f);
		mat_rotate(m, turn[i], 0.0f, 0.0f, 1.0f);
		mat_mul(mvp, proj, m);
		glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
		glUniformMatrix4fv(u_normal, 1, GL_FALSE, m);	/* rotations only: its own inverse transpose */
		glUniform4fv(u_colour, 1, gear[i].colour);
		glBindBuffer(GL_ARRAY_BUFFER, gear[i].vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
		glDrawArrays(GL_TRIANGLES, 0, gear[i].count);
	}
}

/* ---------------------------------------------------------------- the window */

static TS_UUID		win;
static TS_EGLWINDOW	ewin;
static ID		port;

static int window_open( void )
{
	static const char	*json = "{\"rect\":[120,90,460,420],\"attr\":7}";
	T_OBCRE			c;
	T_OBNTF			req;
	T_OBWPOS		wp;
	TS_UUID			ch;
	SZ			asz = 0;

	memset(&c, 0, sizeof(c));
	c.type = OB_T_WINDOW;
	c.sub = OB_S_WINDOW;
	c.name = (CONST UB *)"歯車";
	c.json = (CONST UB *)json;
	c.jsonsz = (SZ)strlen(json);
	if ( ob_cre_obj(&c, &win) < E_OK ) return 0;
	ewin.key = ob_opn_obj(&win, OB_OP_R | OB_OP_W | OB_OP_PROT);
	if ( ewin.key <= 0 ) return 0;
	/* the work area as large as asked, whatever the frame takes */
	if ( ob_rea_rec(ewin.key, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz) >= E_OK && asz >= (SZ)sizeof(wp) ) {
		wp.right += WORK_W - ( wp.wright - wp.wleft );
		wp.bottom += WORK_H - ( wp.wbottom - wp.wtop );
		(void)ob_wri_rec(ewin.key, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz);
	}
	memset(&c, 0, sizeof(c));
	c.type = OB_T_CHANNEL;
	if ( ob_cre_obj(&c, &ch) < E_OK ) return 0;
	port = ob_opn_obj(&ch, OB_OP_READ | OB_OP_WRITE | OB_O_NOWAIT);
	if ( port <= 0 ) return 0;
	memset(&req, 0, sizeof(req));
	req.events = OB_E_CLOSE | OB_E_DELETE | OB_E_KEY;
	return ob_ntf_evt(ewin.key, OB_REC_ANY, &req, port) > 0;
}

/* What happened to the window; whether to go on */
static int events( void )
{
	T_OBNTM	m;
	SZ	asz = 0;

	while ( ob_rea_rec(port, 0, 0, &m, sizeof(m), &asz) >= E_OK ) {
		if ( m.event == OB_E_CLOSE || m.event == OB_E_DELETE ) return 0;
	}
	return 1;
}

int main( void )
{
	EGLDisplay	dpy;
	EGLConfig	cfg;
	EGLContext	ctx;
	EGLSurface	surf;
	EGLint		n, w = WORK_W, h = WORK_H;
	EGLint		ca[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE
	};
	EGLint		xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	char		arg[64];
	SZ		an = ts_get_arg(arg, sizeof(arg) - 1);
	long		frames_max = -1, frames = 0, t0, t_last;
	int		last_count = 0;
	long long	u0, u1, u2, draw_us = 0, swap_us = 0, draw_max = 0, swap_max = 0;
	GLfloat		angle = 0.0f;

	if ( an < 0 ) an = 0;
	arg[an] = '\0';
	if ( strncmp(arg, "frames=", 7) == 0 ) frames_max = atol(arg + 7);

	if ( !window_open() ) {
		printf("glgears: no window\n");
		return 1;
	}
	dpy = eglGetPlatformDisplay(EGL_PLATFORM_TESSRONOS, NULL, NULL);
	if ( dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)
	  || !eglChooseConfig(dpy, ca, &cfg, 1, &n) || n < 1 ) {
		printf("glgears: no EGL display or config\n");
		(void)ob_del_obj(&win);
		return 2;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
	surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)&ewin, NULL);
	if ( ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE || !eglMakeCurrent(dpy, surf, surf, ctx)
	  || !setup() ) {
		printf("glgears: no context or surface (EGL error %x)\n", eglGetError());
		(void)ob_del_obj(&win);
		return 3;
	}
	printf("glgears: %s, %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	t0 = t_last = now_ms();
	while ( events() ) {
		long t = now_ms();

		eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
		eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
		if ( w > 0 && h > 0 ) {
			angle = 70.0f * (GLfloat)( t - t0 ) / 1000.0f;	/* degrees a second, whatever the speed */
			u0 = now_us();
			draw(w, h, angle);
			u1 = now_us();
			eglSwapBuffers(dpy, surf);
			u2 = now_us();
			draw_us += u1 - u0;
			swap_us += u2 - u1;
			if ( u1 - u0 > draw_max ) draw_max = u1 - u0;
			if ( u2 - u1 > swap_max ) swap_max = u2 - u1;
		} else {
			tk_dly_tsk(50);
		}
		frames++;
		if ( t - t_last >= 5000 ) {
			int done = (int)( frames - last_count );

			printf("glgears: %d frames in %ld.%01ld seconds = %ld.%01ld fps (%dx%d); "
			       "drawing %ld us (most %ld), eglSwapBuffers %ld us (most %ld) a frame\n", done,
			       ( t - t_last ) / 1000, ( t - t_last ) % 1000 / 100,
			       done * 1000L / ( t - t_last ), done * 10000L / ( t - t_last ) % 10, w, h,
			       (long)( ( done > 0 ) ? draw_us / done : 0 ), (long)draw_max,
			       (long)( ( done > 0 ) ? swap_us / done : 0 ), (long)swap_max);
			draw_us = swap_us = draw_max = swap_max = 0;
			t_last = t;
			last_count = (int)frames;
		}
		if ( frames_max >= 0 && frames >= frames_max ) break;
	}
	printf("glgears: %ld frames in %ld ms\n", frames, now_ms() - t0);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surf);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	(void)ob_del_obj(&win);
	return 0;
}
