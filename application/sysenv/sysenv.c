/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sysenv.c
 *	システム環境設定: the accessory (design 16.5.23)
 *
 *	What the machine is and how it is set:
 *	  〈機器〉〈機器情報〉 the devices; [表示] opens the page of the one
 *			      chosen -- the screen's size, the network card,
 *			      the USB devices, the disks, the video, the
 *			      keyboard, the sound: where it goes (chosen at
 *			      once and kept), what plays, its volume, and
 *			      〈試聴〉
 *	  〈機器〉〈電源管理〉 how the machine is powered, and 再起動 and
 *			      電源を切る; the limit of the processors' clock,
 *			      the clock now with the temperature, what the
 *			      firmware says of the supply, and the watchdog,
 *			      the limit and the watchdog
 *			      kept with [設定]
 *	  〈画面〉〈背景〉	   the wallpaper, one of the wallpaper box's pictures
 *	  〈画面〉〈配色〉	   the colour scheme
 *	  〈画面〉〈ウィンドウ〉 the windows' look: how far the frame, or the
 *			      whole window, is seen through, the colours
 *			      washed over the frame and the title band, the
 *			      corners, the shadow, the spread of what shows
 *			      through, the opening and closing, and whether a
 *			      window dragged moves as it goes; each takes at
 *			      once, and is kept a moment after the last change
 *			      and when the window closes; 初期値 and 取り消し
 *			      put back what nothing says and what there was
 *			      when the window opened
 *	  〈書体〉		   the faces of the font box, each used or not;
 *			   [詳細情報] opens フォント設定 for the one chosen;
 *			   a face saved, or a font file, dropped on the list
 *			   is registered; 保存 and 削除 in the menu's 操作
 *			   save the one chosen as an object and delete it
 *	  〈バージョン〉	   the system and the programs of the program box,
 *			   each with its version (R1.000) at the right end;
 *			   [詳細情報] shows the one chosen; a package (.tpk)
 *			   dropped on the list is installed, or updates the
 *			   program it is a newer version of; 削除 in the
 *			   menu's 操作 deletes the one chosen
 *
 *	Everything is read and changed through objects: what the machine is
 *	from the system object's records (SYSDEF_SYSTEM, OB_SYS_*), which
 *	also take the scheme, the wallpaper and the power when written; what
 *	is kept from one start to the next in the machine's settings
 *	(SYSDEF_CONF_DEV), which the system takes again as it is written;
 *	the disks as the device objects they are, and the pictures, faces
 *	and programs as the objects their boxes link to.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ts/ui.h>
#include <ts/conf.h>
#include <ts/sysdef.h>
#include <ts/look.h>
#include <ts/json.h>
#include <ts/dtreq.h>
#include <ts/snd.h>

#define TICK_MS		20
#define TEXT_MAX	4096
#define LIST_MAX	64

/* The sheets: the tags, and the devices' pages opened from 〈機器情報〉 */
#define S_DEVS		1
#define S_POWER		2
#define S_WALL		3
#define S_SCHEME	4
#define S_LOOK		5
#define S_FONT		6
#define S_VERSION	7
#define S_D_SCREEN	8
#define S_D_NET		9
#define S_D_USB		10
#define S_D_DISK	11
#define S_D_VIDEO	12
#define S_D_KBD		13
#define S_D_SOUND	14

/* The parts, by the numbers the data box (SYSDEF_BOX_SYSENV) gives them */
#define P_DEVS		1
#define P_SHOW		2
#define P_POWER_NOW	3
#define P_BATTERY	4
#define P_PM		5
#define P_RESTART	6
#define P_OFF		7
#define P_WALLS		8
#define P_WALL_MODE	9
#define P_SCHEMES	11
#define P_FONTS		13
#define P_FONT_INFO	14
#define P_VER_NAME	15
#define P_VER_KERNEL	16
#define P_VER_BUILD	17
#define P_VER_BOARD	18
#define P_VER_CPU	19
#define P_VER_MEM	20
#define P_PROGS		21
#define P_BACK		22
#define P_SCR_LIST	23
#define P_SCR_CANCEL	25
#define P_SCR_SET	26
#define P_NET_1		27
#define P_USB_LIST	33
#define P_USB_AGAIN	34
#define P_DISK_LIST	35
#define P_VID_1		36
#define P_KBD_TYPE	39
#define P_KBD_MODE	40
#define P_KBD_CANCEL	41
#define P_KBD_SET	42
#define P_SND_1		43
#define P_PROG_INFO	46
#define P_SND_OUTS	47
#define P_SND_TEST	48
#define P_SND_VOL	49
#define P_SND_VOL_BOX	81
#define P_SND_KEEP	82
#define P_FONT_USE	83

/* 〈ウィンドウ〉: a volume's value is shown in the box numbered after it */
#define P_LK_ON		50
#define P_LK_OPA	51
#define P_LK_WHOLE	53
#define P_LK_WASH	54
#define P_LK_WASH_STR	55
#define P_LK_BAR	57
#define P_LK_BAR_STR	58
#define P_LK_ROUND	60
#define P_LK_SHD_DARK	62
#define P_LK_SHD_XY	64
#define P_LK_SHD_SIZE	65
#define P_LK_BLUR	67
#define P_LK_MOTION	69
#define P_LK_LIVE	71
#define P_LK_DEFAULT	72
#define P_LK_CANCEL	73
#define P_LK_GPU	74	/* GPUで合成: whatever the look, so never turned off with it */

/* 〈電源管理〉: the processors' clock and the watchdog */
#define P_CPU_MAX	75
#define P_CPU_NOW	76
#define P_SUPPLY	77
#define P_WD_ON		78
#define P_WD_SEC	79
#define P_PERF_SET	80

#define PN_MAIN		1
#define PN_TELL		10
#define PN_POWER	11
#define PN_SCREEN	12
#define PN_PROG		13
#define B_POWER		3
#define B_RESTART	105

/* 〈書体〉's window フォント設定 and the panels it asks with */
#define PN_FONTSET	14
#define PN_FONT_FAMILY	15
#define PN_FONT_END_NEW	16
#define PN_FONT_END_SAME 17
#define PN_FONT_END_UPD	18
#define PN_FONT_REG	19
#define PN_FONT_REG_SAME 20
#define PN_FONT_DEL	21
#define B_FONT_NOT_END	161		/* 登録しないで終了, 更新しないで終了 */
#define B_FONT_REG_END	163		/* 登録して終了 */
#define B_FONT_UPD_END	164		/* 更新して終了 */
#define B_FONT_REG	165		/* 登録 */
#define B_FONT_UPD	166		/* 更新 */
#define B_FONT_DEL	167		/* 削除 */

LOCAL CONST char *CONST dev_names[] = {
	"画面サイズ／色数", "ネットワークアダプタ", "ＵＳＢ接続機器", "ディスク",
	"ビデオアダプタ", "キーボード", "サウンド",
};
LOCAL CONST INT dev_pages[] = { S_D_SCREEN, S_D_NET, S_D_USB, S_D_DISK, S_D_VIDEO, S_D_KBD, S_D_SOUND };
#define NDEV	7

LOCAL UI	ui;
LOCAL BOOL	closing;
LOCAL TS_UUID	*ask_win = NULL;	/* the window the desktop's panels go over, when not ui's */
LOCAL UB	text[TEXT_MAX];

/* What lists show: names kept here */
LOCAL char	wall_name[LIST_MAX][64];
LOCAL TS_UUID	wall_id[LIST_MAX];
LOCAL INT	nwall, wall_cur;
LOCAL char	scheme_name[8][32];
LOCAL INT	nscheme;
LOCAL char	prog_name[LIST_MAX][64];
LOCAL TS_UUID	prog_id[LIST_MAX];
LOCAL INT	nprog;

/* What each program's metadata says (tessronos.program) */
typedef struct {
	char	name[64];
	char	id[40];
	char	kind[16];
	char	version[16];
	BOOL	builtin;
} PROGINFO;
LOCAL PROGINFO	prog_info[LIST_MAX];
LOCAL char	usb_line[LIST_MAX][80];
LOCAL char	disk_line[LIST_MAX][80];
LOCAL char	mode_name[16][40];
LOCAL INT	mode_w[16], mode_h[16], nmode, mode_cur;

/* A list's names, as pointers to what is kept */
LOCAL void names_of( INT num, char (*v)[64], INT n )
{
	CONST char	*p[LIST_MAX];
	INT		i;

	for ( i = 0; i < n && i < LIST_MAX; i++ ) p[i] = v[i];
	ui_names(&ui, num, p, n);
}

/* ---------------------------------------------------------------- objects */

/* A record of an object, into text; its length or an error */
LOCAL INT rec_of( CONST TS_UUID *u, INT recno, UB *buf, INT max )
{
	ID	key;
	SZ	asz = 0;
	ER	er;

	key = ob_opn_obj(u, OB_OP_R);
	if ( key < E_OK ) return (INT)key;
	er = ob_rea_rec(key, recno, 0, buf, max - 1, &asz);
	ob_cls_obj(key);
	if ( er < E_OK ) return (INT)er;
	buf[asz] = 0;
	return (INT)asz;
}

LOCAL INT sys_rec( INT recno )
{
	TS_UUID	u;

	if ( ui_uuid(SYSDEF_SYSTEM, &u) < E_OK ) return E_SYS;
	return rec_of(&u, recno, text, TEXT_MAX);
}

/* The screen's mode, from the screen's own object 画面 (SIZE, BPP, DRIVER, MODES) */
LOCAL INT disp_rec( void )
{
	CONST TS_UUID	u = OB_UUID_DISPLAY_INIT;

	return rec_of(&u, OB_DSP_MODE, text, TEXT_MAX);
}

LOCAL ER sys_write( INT recno, CONST char *s )
{
	TS_UUID	u;
	ID	key;
	SZ	asz = 0;
	ER	er;

	if ( ui_uuid(SYSDEF_SYSTEM, &u) < E_OK ) return E_SYS;
	key = ob_opn_obj(&u, OB_OP_R | OB_OP_WRITE);
	if ( key < E_OK ) return (ER)key;
	er = ob_wri_rec(key, recno, 0, s, (SZ)strlen(s), &asz);
	ob_cls_obj(key);
	return er;
}

/* A line of the machine's settings written; the system takes it as it sees it */
LOCAL ER dev_put( CONST char *key, CONST char *val )
{
	TS_UUID	u;
	ID	k;
	SZ	asz = 0;
	INT	len;
	ER	er;

	if ( ui_uuid(SYSDEF_CONF_DEV, &u) < E_OK ) return E_SYS;
	k = ob_opn_obj(&u, OB_OP_R | OB_OP_WRITE);
	if ( k < E_OK ) return (ER)k;
	er = ob_rea_rec(k, 1, 0, text, TEXT_MAX, &asz);
	len = ( er >= E_OK ) ? (INT)asz : 0;
	len = cf_put(text, len, TEXT_MAX, key, 0, val);
	er = ( len < 0 ) ? E_LIMIT : ob_wri_rec(k, 1, 0, text, len, &asz);
	if ( er >= E_OK ) er = ob_trn_rec(k, 1, (UD)len);
	ob_cls_obj(k);
	return er;
}

LOCAL BOOL dev_get( CONST char *key, char *v, INT max )
{
	TS_UUID	u;
	INT	len;

	if ( ui_uuid(SYSDEF_CONF_DEV, &u) < E_OK ) return FALSE;
	len = rec_of(&u, 1, text, TEXT_MAX);
	return (BOOL)( len > 0 && cf_get(text, len, key, 0, v, max) );
}

/*
 * The objects a box links to (ob_lst_lnk), those that are there, and
 * each one's name.
 */
#define BOX_LINKS_MAX	256

LOCAL INT box_links( CONST char *box, TS_UUID *ids, char (*names)[64], INT max )
{
	TS_UUID	b;
	T_OBREF	r;
	INT	all = 0, i, n = 0;
	static TS_UUID lk[BOX_LINKS_MAX];

	if ( ui_uuid(box, &b) < E_OK ) return 0;
	if ( ob_lst_lnk(&b, NULL, lk, BOX_LINKS_MAX, &all) < E_OK ) return 0;
	for ( i = 0; i < all && i < BOX_LINKS_MAX && n < max; i++ ) {
		if ( ob_ref_obj(&lk[i], &r) < E_OK ) continue;
		ids[n] = lk[i];
		strncpy(names[n], (CONST char *)r.name, 63);
		names[n][63] = 0;
		n++;
	}
	return n;
}

/* ---------------------------------------------------------------- filled from what is */

/* A program object's metadata read: its name, id, kind, version */
LOCAL void prog_read( CONST TS_UUID *u, PROGINFO *pi )
{
	static UB	meta[4096];
	T_JSON		root, tf, g;
	SZ		asz = 0;
	ID		key;

	memset(pi, 0, sizeof(*pi));
	key = ob_opn_obj(u, OB_OP_R);
	if ( key <= 0 ) return;
	if ( ob_get_atr(key, meta, sizeof(meta) - 1, &asz) >= E_OK
	  && js_parse(meta, (INT)asz, &root) >= E_OK
	  && js_get(&root, "tessronos", &tf) >= E_OK && js_get(&tf, "program", &g) >= E_OK ) {
		if ( js_get_str(&g, "name", (UB *)pi->name, sizeof(pi->name)) <= 0 ) {
			(void)js_get_str(&root, "name", (UB *)pi->name, sizeof(pi->name));
		}
		(void)js_get_str(&g, "id", (UB *)pi->id, sizeof(pi->id));
		(void)js_get_str(&g, "kind", (UB *)pi->kind, sizeof(pi->kind));
		(void)js_get_str(&g, "version", (UB *)pi->version, sizeof(pi->version));
		pi->builtin = js_get_bool(&g, "builtin", FALSE);
	}
	ob_cls_obj(key);
}

LOCAL void val_of( INT len, CONST char *key, char *v, INT max )
{
	if ( len <= 0 || !cf_get(text, len, key, 0, v, max) ) snprintf(v, max, "（不明）");
}

LOCAL void fill_version( void )
{
	char	v[CF_VAL_MAX], s[CF_VAL_MAX * 2];
	INT	len = sys_rec(OB_SYS_INFO), i;
	static TS_UUID	ids[LIST_MAX];
	INT	n;

	val_of(len, "NAME", v, sizeof(v));
	if ( len > 0 && cf_get(text, len, "VERSION", 0, s, sizeof(s)) ) {
		strncat(v, " ", sizeof(v) - strlen(v) - 1);
		strncat(v, s, sizeof(v) - strlen(v) - 1);
	}
	ui_set_text(&ui, P_VER_NAME, v);
	val_of(len, "KERNEL", v, sizeof(v));
	ui_set_text(&ui, P_VER_KERNEL, v);
	val_of(len, "BUILD", v, sizeof(v));
	ui_set_text(&ui, P_VER_BUILD, v);
	val_of(len, "BOARD", v, sizeof(v));
	ui_set_text(&ui, P_VER_BOARD, v);
	val_of(len, "CPU", v, sizeof(v));
	snprintf(s, sizeof(s), "%s ×", v);
	val_of(len, "CPUS", v, sizeof(v));
	strncat(s, v, sizeof(s) - strlen(s) - 1);
	ui_set_text(&ui, P_VER_CPU, s);
	val_of(len, "MEMORY_MB", v, sizeof(v));
	snprintf(s, sizeof(s), "%sMB（空き ", v);
	val_of(len, "FREE_MB", v, sizeof(v));
	strncat(s, v, sizeof(s) - strlen(s) - 1);
	strncat(s, "MB）", sizeof(s) - strlen(s) - 1);
	ui_set_text(&ui, P_VER_MEM, s);

	n = box_links(SYSDEF_PROG_BOX, ids, prog_name, LIST_MAX);
	for ( i = 0; i < n; i++ ) {
		prog_id[i] = ids[i];
		prog_read(&ids[i], &prog_info[i]);
		if ( prog_info[i].name[0] == 0 ) {
			snprintf(prog_info[i].name, sizeof(prog_info[i].name), "%s", prog_name[i]);
		}
		/* the name, and the version against the right end */
		snprintf(prog_name[i], sizeof(prog_name[i]), "%s\t%s", prog_info[i].name,
			 ( prog_info[i].version[0] != 0 ) ? prog_info[i].version : "－");
	}
	nprog = n;
	names_of(P_PROGS, prog_name, n);
	ui_set_value(&ui, P_PROGS, 0);
}

LOCAL void fill_perf( void );

LOCAL void fill_power( void )
{
	char	v[CF_VAL_MAX];
	INT	len = sys_rec(OB_SYS_POWER);

	ui_set_text(&ui, P_POWER_NOW, "ＡＣ電源");
	ui_set_text(&ui, P_BATTERY, "なし");
	ui_set_text(&ui, P_PM, "対応しない");
	if ( len <= 0 || !cf_get(text, len, "CAN", 0, v, sizeof(v)) || strstr(v, "RESTART") == NULL ) {
		ui_off(&ui, P_RESTART, TRUE);
		ui_off(&ui, P_OFF, TRUE);
	}
	fill_perf();
}

/*
 * 〈電源管理〉's clock and watchdog. The machine's settings CPU_MHZ (the
 * limit in MHz, 0 the most the firmware allows, CPU_DEFAULT with no
 * line) and WATCHDOG (seconds, 0 off) are what the system takes; what
 * it made of them is read back from the system object's record of the
 * hardware (OB_SYS_HW), which the firmware's answers fill.
 */
#define CPU_DEFAULT	1800
#define WD_DEFAULT	30
#define PERF_TICKS	100		/* the state read again every two seconds while shown */
#define PERF_SOON	30		/* and soon after a setting, once the system has taken it */

LOCAL CONST INT	cpu_mhz_of[] = { 1500, 1800, 2000, 2200, 0 };
#define NCPU		( (INT)( sizeof(cpu_mhz_of) / sizeof(cpu_mhz_of[0]) ) )

LOCAL INT	perf_tick;
LOCAL char	perf_said[CF_VAL_MAX * 2];

/* A rate in Hz of the record, in MHz: 2.4GHz is beyond what cf_num's INT holds; 0 when not a number */
LOCAL INT mhz_of( INT len, CONST char *key )
{
	char	v[CF_VAL_MAX];
	UINT	u = 0;
	INT	i;

	if ( len <= 0 || !cf_get(text, len, key, 0, v, sizeof(v)) ) return 0;
	for ( i = 0; v[i] >= '0' && v[i] <= '9'; i++ ) u = u * 10 + (UINT)( v[i] - '0' );
	return ( i > 0 && v[i] == 0 ) ? (INT)( u / 1000000 ) : 0;
}

LOCAL void fill_perf_state( void )
{
	char	v[CF_VAL_MAX], s[CF_VAL_MAX * 2], w[CF_VAL_MAX + 16];
	INT	len = sys_rec(OB_SYS_HW), hz, lim, mx;
	UINT	thr;

	perf_tick = 0;
	hz = mhz_of(len, "ARM_HZ");
	mx = mhz_of(len, "ARM_MAX_HZ");
	lim = ( len > 0 && cf_get(text, len, "ARM_LIMIT_MHZ", 0, v, sizeof(v)) ) ? cf_num(v, -1) : -1;
	if ( hz > 0 ) {
		snprintf(s, sizeof(s), "%dMHz", hz);
	} else {
		snprintf(s, sizeof(s), "（不明）");
	}
	if ( lim == 0 ) {
		strncat(s, "　上限 最大", sizeof(s) - strlen(s) - 1);
	} else if ( lim > 0 ) {
		snprintf(w, sizeof(w), "　上限 %dMHz", lim);
		strncat(s, w, sizeof(s) - strlen(s) - 1);
	}
	if ( mx > 0 ) {
		snprintf(w, sizeof(w), "（最大 %dMHz）", mx);
		strncat(s, w, sizeof(s) - strlen(s) - 1);
	}
	if ( len > 0 && cf_get(text, len, "TEMPERATURE", 0, v, sizeof(v)) && strcmp(v, "unknown") != 0 ) {
		snprintf(w, sizeof(w), "　%s℃", v);
		strncat(s, w, sizeof(s) - strlen(s) - 1);
	}
	ui_set_text(&ui, P_CPU_NOW, s);
	if ( strcmp(s, perf_said) != 0 ) {
		/* told when it changes, for the tests of the screen */
		snprintf(perf_said, sizeof(perf_said), "%s", s);
		tm_putstring((CONST UB *)"sysenv: clock ");
		tm_putstring((CONST UB *)s);
		tm_putstring((CONST UB *)"\n");
	}

	thr = ( len > 0 && cf_get(text, len, "THROTTLED", 0, v, sizeof(v)) ) ? (UINT)cf_num(v, 0) : 0;
	if ( len <= 0 || !cf_get(text, len, "UNDERVOLT", 0, v, sizeof(v)) || strcmp(v, "unknown") == 0 ) {
		snprintf(s, sizeof(s), "（不明）");
	} else if ( strcmp(v, "now") == 0 ) {
		snprintf(s, sizeof(s), "低電圧（電源が足りません）");
	} else if ( strcmp(v, "before") == 0 ) {
		snprintf(s, sizeof(s), "正常（起動後に低電圧がありました）");
	} else {
		snprintf(s, sizeof(s), "正常");
	}
	if ( thr & 0x6U ) {
		strncat(s, "　クロック制限中", sizeof(s) - strlen(s) - 1);
	}
	ui_set_text(&ui, P_SUPPLY, s);
}

LOCAL void fill_perf( void )
{
	char	v[CF_VAL_MAX];
	INT	mhz = CPU_DEFAULT, wd = WD_DEFAULT, i, f[1];

	if ( dev_get("CPU_MHZ", v, sizeof(v)) ) {
		mhz = cf_num(v, CPU_DEFAULT);
		if ( mhz < 0 ) mhz = CPU_DEFAULT;
	}
	for ( i = 0; i < NCPU && cpu_mhz_of[i] != mhz; i++ ) ;
	ui_set_value(&ui, P_CPU_MAX, ( i < NCPU ) ? i + 1 : 0);
	if ( dev_get("WATCHDOG", v, sizeof(v)) ) {
		wd = cf_num(v, WD_DEFAULT);
		if ( wd < 0 ) wd = WD_DEFAULT;
	}
	ui_set_value(&ui, P_WD_ON, wd > 0);
	f[0] = ( wd > 0 ) ? wd : WD_DEFAULT;
	ui_set_fields(&ui, P_WD_SEC, f, 1);
	ui_off(&ui, P_WD_SEC, wd <= 0);
	{
		char	s[80];

		snprintf(s, sizeof(s), "sysenv: settings CPU_MHZ %d WATCHDOG %d\n", mhz, wd);
		tm_putstring((CONST UB *)s);
	}
	fill_perf_state();
}

LOCAL void fill_walls( void )
{
	char	v[CF_VAL_MAX], w[48], m[8];
	INT	i, len;

	nwall = box_links(SYSDEF_WALL_BOX, wall_id, wall_name, LIST_MAX);
	names_of(P_WALLS, wall_name, nwall);
	wall_cur = 0;
	len = sys_rec(OB_SYS_WALL);
	if ( len > 0 && cf_get(text, len, "WALLPAPER", 0, v, sizeof(v)) && cf_word(v, 0, w, sizeof(w)) ) {
		TS_UUID	u;

		if ( ui_uuid(w, &u) >= E_OK ) {
			for ( i = 0; i < nwall; i++ ) {
				if ( memcmp(&u, &wall_id[i], sizeof(u)) == 0 ) wall_cur = i + 1;
			}
		}
	}
	ui_set_now(&ui, P_WALLS, wall_cur, wall_cur);
	ui_set_value(&ui, P_WALL_MODE, ( len > 0 && cf_get(text, len, "MODE", 0, m, sizeof(m)) ) ? cf_num(m, 2) + 1 : 3);
}

LOCAL void fill_schemes( void )
{
	char	v[CF_VAL_MAX];
	CONST char *nm[8];
	INT	len = sys_rec(OB_SYS_SCHEME), cur;

	nscheme = 0;
	while ( len > 0 && nscheme < 8 && cf_get(text, len, "NAME", nscheme, scheme_name[nscheme], 32) ) {
		nm[nscheme] = scheme_name[nscheme];
		nscheme++;
	}
	ui_names(&ui, P_SCHEMES, nm, nscheme);
	cur = ( len > 0 && cf_get(text, len, "SCHEME", 0, v, sizeof(v)) ) ? cf_num(v, 0) + 1 : 1;
	ui_set_now(&ui, P_SCHEMES, cur, cur);
}

/* The screen's page: the sizes it may be started at, the one it has underlined */
LOCAL void fill_screen( void )
{
	char	v[CF_VAL_MAX], w[16], cur[32] = "";
	CONST char *nm[16];
	INT	len = disp_rec(), i;

	if ( len > 0 ) (void)cf_get(text, len, "SIZE", 0, cur, sizeof(cur));
	nmode = 0;
	mode_cur = 0;
	if ( len > 0 && cf_get(text, len, "MODES", 0, v, sizeof(v)) ) {
		for ( i = 0; nmode < 16 && cf_word(v, i, w, sizeof(w)); i++ ) {
			INT	a = 0, b = 0;
			CONST char *x = strchr(w, 'x');

			if ( x == NULL ) continue;
			a = cf_num(w, 0);
			{
				char	ww[8];
				INT	k = (INT)( x - w );

				if ( k > 7 ) continue;
				memcpy(ww, w, k);
				ww[k] = 0;
				a = cf_num(ww, 0);
			}
			b = cf_num(x + 1, 0);
			mode_w[nmode] = a;
			mode_h[nmode] = b;
			snprintf(mode_name[nmode], sizeof(mode_name[nmode]), "%d×%d：１６７７万色", a, b);
			nm[nmode] = mode_name[nmode];
			if ( strcmp(w, cur) == 0 ) mode_cur = nmode + 1;
			nmode++;
		}
	}
	ui_names(&ui, P_SCR_LIST, nm, nmode);
	ui_set_now(&ui, P_SCR_LIST, mode_cur, mode_cur);
}

LOCAL void fill_net( void )
{
	char	v[CF_VAL_MAX], s[CF_VAL_MAX * 2];
	INT	len = sys_rec(OB_SYS_NET);

	val_of(len, "ADAPTER", v, sizeof(v));
	ui_set_text(&ui, P_NET_1, v);
	val_of(len, "MAC", v, sizeof(v));
	ui_set_text(&ui, ( P_NET_1 + 1 ), v);
	val_of(len, "LINK", v, sizeof(v));
	ui_set_text(&ui, ( P_NET_1 + 2 ), strcmp(v, "up") == 0 ? "接続" : strcmp(v, "down") == 0 ? "未接続" : v);
	val_of(len, "ADDRESS", v, sizeof(v));
	ui_set_text(&ui, ( P_NET_1 + 3 ), v);
	val_of(len, "DHCP", v, sizeof(v));
	ui_set_text(&ui, ( P_NET_1 + 4 ), strcmp(v, "on") == 0 ? "使用する" : "使用しない");
	val_of(len, "SENT", v, sizeof(v));
	snprintf(s, sizeof(s), "送信 %s／受信 ", v);
	val_of(len, "RECEIVED", v, sizeof(v));
	strncat(s, v, sizeof(s) - strlen(s) - 1);
	strncat(s, " フレーム", sizeof(s) - strlen(s) - 1);
	ui_set_text(&ui, ( P_NET_1 + 5 ), s);
}

LOCAL void fill_usb( void )
{
	CONST char *nm[LIST_MAX];
	INT	len = sys_rec(OB_SYS_USB), n = 0;

	while ( len > 0 && n < LIST_MAX && cf_get(text, len, "DEVICE", n, usb_line[n], sizeof(usb_line[n])) ) {
		nm[n] = usb_line[n];
		n++;
	}
	if ( n == 0 ) {
		strcpy(usb_line[0], "（接続された機器はありません）");
		nm[0] = usb_line[0];
		n = 1;
	}
	ui_names(&ui, P_USB_LIST, nm, n);
	ui_set_value(&ui, P_USB_LIST, 0);
}

/* The disks: the device objects that are disks, their size and where they are mounted */
LOCAL void fill_disks( void )
{
	CONST char *nm[LIST_MAX];
	static TS_UUID	ids[64];
	INT	cnt = 0, i, n = 0;

	if ( ob_lst_obj(OB_T_DEVICE, OB_S_DISK, NULL, ids, 64, &cnt) < E_OK ) cnt = 0;
	for ( i = 0; i < cnt && n < LIST_MAX; i++ ) {
		T_OBREF	r;
		T_OBREC	rec[2];
		INT	nr = 0;
		ID	key;
		UD	size = 0;
		SZ	asz = 0;
		static UB meta[2048];
		char	where[64] = "";

		if ( ob_ref_obj(&ids[i], &r) < E_OK ) continue;
		key = ob_opn_obj(&ids[i], OB_OP_ATRRD);
		if ( key > 0 ) {
			if ( ob_lst_rec(key, rec, 2, &nr) >= E_OK && nr > 1 ) size = rec[1].size;
			if ( ob_get_atr(key, meta, sizeof(meta) - 1, &asz) >= E_OK ) {
				CONST char *m;

				meta[asz] = 0;
				m = strstr((CONST char *)meta, "\"path\":\"");
				if ( m != NULL ) {
					INT	k;

					m += 8;
					for ( k = 0; m[k] != 0 && m[k] != '"' && k < 63; k++ ) where[k] = m[k];
					where[k] = 0;
				}
			}
			ob_cls_obj(key);
		}
		snprintf(disk_line[n], sizeof(disk_line[n]), "%s　%d MB　%s", (CONST char *)r.name,
			 (INT)( size >> 20 ), where);
		nm[n] = disk_line[n];
		n++;
	}
	ui_names(&ui, P_DISK_LIST, nm, n);
	ui_set_value(&ui, P_DISK_LIST, 0);
}

LOCAL void fill_video( void )
{
	char	v[CF_VAL_MAX];
	INT	len = disp_rec();

	val_of(len, "SIZE", v, sizeof(v));
	ui_set_text(&ui, P_VID_1, v);
	val_of(len, "BPP", v, sizeof(v));
	strncat(v, " ビット／画素（１６７７万色）", sizeof(v) - strlen(v) - 1);
	ui_set_text(&ui, ( P_VID_1 + 1 ), v);
	val_of(len, "DRIVER", v, sizeof(v));
	ui_set_text(&ui, ( P_VID_1 + 2 ), v);
}

LOCAL void fill_kbd( void )
{
	char	v[CF_VAL_MAX];

	ui_set_now(&ui, P_KBD_TYPE, ( dev_get("KBTYPE", v, sizeof(v)) && cf_num(v, 0x41) == 0x40 ) ? 2 : 1,
		   ( wm_look(LK_KBD_US) != 0 ) ? 2 : 1);
	ui_set_value(&ui, P_KBD_MODE, ( dev_get("KBMODE", v, sizeof(v)) && ( cf_num(v, 1) & 3 ) == 0 ) ? 1 : 2);
}

/*
 * 〈サウンド〉: the outputs the system object's record of sound has
 * (OUT, OUT_KIND, OUT_FLAGS, OUT_NAME, a set for each), "auto" first.
 * Choosing one takes it at once (the record written SELECT and the id)
 * and keeps it for the next start (the machine's settings' SOUND_OUT).
 * An output chosen that is not there now is shown after the others, so
 * what is chosen can always be seen. The page is read again every
 * second while it shows, so a USB device plugged in or out shows up.
 * 〈試聴〉 has the system play half a second of a tone, as it plays its
 * buzzer, on whatever it plays on; the volume is the whole's volume of
 * the mixer (both sides together; 音量ミキサー sets them apart), the
 * same on every output.
 */
#define SND_MAX		10
LOCAL void tell( CONST char *line );
LOCAL BOOL is_code( CONST T_MNSEL *sel, CONST char *code );
#define SND_TICKS	50		/* the page read again every second */

LOCAL char	snd_id[SND_MAX][SND_ID_MAX];
LOCAL char	snd_line[SND_MAX][96];
LOCAL INT	nsnd, snd_tick;
LOCAL char	snd_said[TEXT_MAX / 4];	/* the record as last shown */
LOCAL ID	snd_key = 0;		/* the sound device's object, while a tone may play */

/* The sound device's object, opened once */
LOCAL ID snd_dev( void )
{
	TS_UUID	u;
	INT	cnt = 0;

	if ( snd_key > 0 ) return snd_key;
	if ( ob_lst_obj(OB_T_DEVICE, OB_S_SOUND, NULL, &u, 1, &cnt) < E_OK || cnt < 1 ) return E_NOEXS;
	snd_key = ob_opn_obj(&u, OB_OP_R | OB_OP_WRITE);
	return snd_key;
}

/* An output as a person reads it */
LOCAL void snd_name( CONST char *id, CONST char *kind, CONST char *name, INT flags, char *out, INT max )
{
	CONST char *at = strchr(id, '@');

	if ( strcmp(id, "auto") == 0 ) {
		snprintf(out, max, "自動（ＵＳＢ、Ｉ２Ｓ、ＨＤＭＩの順）");
	} else if ( strcmp(kind, "i2s") == 0 ) {
		snprintf(out, max, "Ｉ２ＳのＤＡＣ（ＧＰＩＯ１８〜２１）");
	} else if ( strcmp(kind, "hdmi") == 0 ) {
		snprintf(out, max, "%s：%s", ( strcmp(id, "hdmi1") == 0 ) ? "ＨＤＭＩ１" : "ＨＤＭＩ０",
			 ( name[0] != 0 ) ? name : "画面");
	} else {
		snprintf(out, max, "ＵＳＢ：%s（%s）", ( name[0] != 0 ) ? name : "オーディオ機器",
			 ( at != NULL ) ? at + 1 : "");
	}
	if ( ( flags & SND_OF_NOSOUND ) != 0 ) {
		strncat(out, "　音を出せません", max - strlen(out) - 1);
	}
}

/* The volume of the output in use, or its part off */
/* The whole's volume, as the mixer has it (the left's; 音量ミキサー sets the two apart) */
LOCAL SDMix	snd_mix;

LOCAL BOOL mix_read( void )
{
	SZ	asz = 0;
	ID	k = snd_dev();

	return (BOOL)( k > 0 && ob_rea_rec(k, OB_SND_MIX, 0, &snd_mix, sizeof(snd_mix), &asz) >= E_OK
		       && asz >= (SZ)sizeof(snd_mix) );
}

LOCAL void snd_volume( void )
{
	char	t[16];

	if ( mix_read() ) {
		ui_off(&ui, P_SND_VOL, FALSE);
		ui_set_range(&ui, P_SND_VOL, (INT)snd_mix.lv, 100, 0);
		snprintf(t, sizeof(t), "%d％", (INT)snd_mix.lv);
		ui_set_text(&ui, P_SND_VOL_BOX, t);
	} else {
		ui_set_range(&ui, P_SND_VOL, 0, 100, 0);
		ui_off(&ui, P_SND_VOL, TRUE);
		ui_set_text(&ui, P_SND_VOL_BOX, "なし");
	}
}

LOCAL void fill_sound( void )
{
	char	v[CF_VAL_MAX], kind[16], name[CF_VAL_MAX], sel[SND_ID_MAX], now[SND_ID_MAX], fl[16];
	CONST char *nm[SND_MAX];
	INT	len = sys_rec(OB_SYS_SOUND), k, cur = 0, playing = 0, flags;

	snd_tick = 0;
	if ( len > 0 ) {
		snprintf(snd_said, sizeof(snd_said), "%.*s", len, (CONST char *)text);
	} else {
		snd_said[0] = 0;
	}
	if ( len <= 0 || !cf_get(text, len, "SELECT", 0, sel, sizeof(sel)) ) strcpy(sel, "auto");
	if ( len <= 0 || !cf_get(text, len, "OUTPUT", 0, now, sizeof(now)) ) strcpy(now, "none");
	nsnd = 0;
	for ( k = 0; len > 0 && nsnd < SND_MAX - 1 && cf_get(text, len, "OUT", k, v, sizeof(v)); k++ ) {
		if ( !cf_get(text, len, "OUT_KIND", k, kind, sizeof(kind)) ) strcpy(kind, "none");
		if ( !cf_get(text, len, "OUT_NAME", k, name, sizeof(name)) ) name[0] = 0;
		flags = cf_get(text, len, "OUT_FLAGS", k, fl, sizeof(fl)) ? cf_num(fl, 0) : 0;
		snprintf(snd_id[nsnd], SND_ID_MAX, "%s", v);
		snd_name(v, kind, name, flags, snd_line[nsnd], sizeof(snd_line[nsnd]));
		if ( ( flags & SND_OF_CHOSEN ) != 0 ) cur = nsnd + 1;
		if ( strcmp(v, now) == 0 ) playing = nsnd + 1;
		nm[nsnd] = snd_line[nsnd];
		nsnd++;
	}
	if ( cur == 0 && nsnd < SND_MAX ) {
		/* chosen, and not there now: shown all the same */
		snprintf(snd_id[nsnd], SND_ID_MAX, "%s", sel);
		snprintf(snd_line[nsnd], sizeof(snd_line[nsnd]), "%s（つながっていません）",
			 ( strncmp(sel, "usb:", 4) == 0 ) ? "前に選んだＵＳＢ機器" : sel);
		nm[nsnd] = snd_line[nsnd];
		cur = ++nsnd;
	}
	ui_names(&ui, P_SND_OUTS, nm, nsnd);
	ui_set_now(&ui, P_SND_OUTS, cur, playing);
	ui_set_value(&ui, P_SND_KEEP, ( len > 0 && cf_get(text, len, "KEEP", 0, v, sizeof(v)) && cf_num(v, 0) != 0 ));

	if ( playing > 0 ) {
		ui_set_text(&ui, P_SND_1, snd_line[playing - 1]);
	} else {
		ui_set_text(&ui, P_SND_1, ( strcmp(now, "none") == 0 ) ? "（いまは鳴っていません）" : now);
	}
	val_of(len, "RATE", v, sizeof(v));
	if ( cf_num(v, 0) > 0 ) {
		strncat(v, " Hz", sizeof(v) - strlen(v) - 1);
	} else {
		snprintf(v, sizeof(v), "－");
	}
	ui_set_text(&ui, ( P_SND_1 + 1 ), v);
	val_of(len, "CHANNELS", v, sizeof(v));
	ui_set_text(&ui, ( P_SND_1 + 2 ), ( cf_num(v, 0) == 2 ) ? "ステレオ" : ( cf_num(v, 0) == 1 ) ? "モノラル" : "－");
	snd_volume();
	{
		char	s[96];

		snprintf(s, sizeof(s), "sysenv: sound %d outputs, chosen %s, playing %s\n", nsnd, sel, now);
		tm_putstring((CONST UB *)s);
	}
}

/* The page read again while it shows; shown again only when the record changed */
LOCAL void sound_tick( void )
{
	INT	len;

	if ( ++snd_tick < SND_TICKS ) return;
	snd_tick = 0;
	len = sys_rec(OB_SYS_SOUND);
	if ( len > 0 && ( (INT)strlen(snd_said) != len || memcmp(snd_said, text, len) != 0 ) ) {
		fill_sound();
	}
}

/* An output chosen: taken now, kept for the next start */
LOCAL void sound_set( void )
{
	INT	k = ui_value(&ui, P_SND_OUTS) - 1;
	char	v[SND_ID_MAX + 8], s[96];
	ER	er;

	if ( k < 0 || k >= nsnd ) return;
	snprintf(v, sizeof(v), "SELECT %s", snd_id[k]);
	er = sys_write(OB_SYS_SOUND, v);
	if ( er >= E_OK ) er = dev_put("SOUND_OUT", snd_id[k]);
	snprintf(s, sizeof(s), "sysenv: sound output %s (%d)\n", snd_id[k], (INT)er);
	tm_putstring((CONST UB *)s);
	if ( er == E_OACV ) {
		tell("機器の設定を変更する権限がありません。");
	} else if ( er < E_OK ) {
		tell("設定した内容が保存できません。");
	}
	(void)tk_dly_tsk(100);			/* the stream moved, then read */
	fill_sound();
}

/*
 * Half a second of 880Hz, played as the system plays its sounds, on the
 * output chosen: asked of the system object's record 8 (PLAY test),
 * which answers whether it sounds.
 */
LOCAL void sound_test( void )
{
	ER	er = sys_write(OB_SYS_SOUND, "PLAY test");
	char	s[64];

	snprintf(s, sizeof(s), "sysenv: sound test %d\n", (INT)er);
	tm_putstring((CONST UB *)s);
	if ( er == E_NOSPT || er == E_NOEXS ) {
		tell("音を出せる出力先がありません。");
		return;
	} else if ( er < E_OK ) {
		tell("音を鳴らせませんでした。");
		return;
	}
	(void)tk_dly_tsk(100);
	fill_sound();
}

/*
 * 出力を止めずに流し続ける: the stream kept going in silence while
 * nothing plays (for a screen that would cut the start of a short
 * sound), taken at once and kept for the next start.
 */
LOCAL void sound_keep( void )
{
	BOOL	on = ( ui_value(&ui, P_SND_KEEP) != 0 );
	char	s[64];
	ER	er;

	er = sys_write(OB_SYS_SOUND, on ? "KEEP 1" : "KEEP 0");
	if ( er >= E_OK ) er = dev_put("SOUND_KEEP", on ? "1" : "0");
	snprintf(s, sizeof(s), "sysenv: sound keep %d (%d)", (INT)on, (INT)er);
	tm_putstring((CONST UB *)s);
	tm_putstring((CONST UB *)"\n");
	if ( er == E_OACV ) {
		tell("機器の設定を変更する権限がありません。");
	} else if ( er < E_OK ) {
		tell("設定した内容が保存できません。");
	}
}

/* The volume worked: the output in use has it at once */
LOCAL void sound_level( void )
{
	SDMixSet	v;
	SZ		asz = 0;
	ID		k = snd_dev();
	INT		pct = ui_value(&ui, P_SND_VOL);
	char		t[16];
	ER		er;

	if ( k <= 0 ) return;
	if ( pct < 0 ) pct = 0;
	if ( pct > 100 ) pct = 100;
	memset(&v, 0, sizeof(v));
	v.what = SND_MIX_ALL;
	v.lv = v.rv = (UW)pct;
	v.mute = ( mix_read() && snd_mix.mute != 0 );
	er = ob_wri_rec(k, OB_SND_MIXSET, 0, &v, sizeof(v), &asz);
	snprintf(t, sizeof(t), "%d％", pct);
	ui_set_text(&ui, P_SND_VOL_BOX, t);
	{
		char	s[64];

		snprintf(s, sizeof(s), "sysenv: sound volume %d (%d)\n", pct, (INT)er);
		tm_putstring((CONST UB *)s);
	}
}

/* ---------------------------------------------------------------- what is done */

LOCAL void tell( CONST char *line )
{
	CONST char *l[1];

	l[0] = line;
	(void)ui_ask(&ui, PN_TELL, l, 1);
}

LOCAL void power( BOOL restart )
{
	CONST char *l[3];

	l[0] = restart ? "システムを再起動します。" : "システムを終了して電源を切ります。";
	l[1] = "開いているウィンドウは保存して閉じます。";
	l[2] = restart ? "再起動" : "電源を切る";
	if ( ui_ask(&ui, PN_POWER, l, 3) != B_POWER ) return;
	if ( sys_write(OB_SYS_POWER, restart ? "RESTART" : "OFF") < E_OK ) {
		tell("電源の操作ができません。");
		return;
	}
	closing = TRUE;
}

/*
 * The limit of the clock and the watchdog kept in the machine's
 * settings, which the system takes as it sees them written: the person
 * at the machine may set both (design 8.7.1), as the settings object
 * lets the users' group write it.
 */
LOCAL void perf_set( void )
{
	INT	k = ui_value(&ui, P_CPU_MAX) - 1, f[1] = { WD_DEFAULT }, wd;
	char	v[16], s[80];
	ER	er = E_OK;

	if ( k >= 0 && k < NCPU ) {
		snprintf(v, sizeof(v), "%d", cpu_mhz_of[k]);
		er = dev_put("CPU_MHZ", v);
	}
	(void)ui_fields(&ui, P_WD_SEC, f, 1);
	wd = ( ui_value(&ui, P_WD_ON) != 0 ) ? f[0] : 0;
	if ( wd != 0 && wd < 5 ) wd = 5;
	if ( wd > 3600 ) wd = 3600;
	snprintf(v, sizeof(v), "%d", wd);
	if ( er >= E_OK ) er = dev_put("WATCHDOG", v);
	snprintf(s, sizeof(s), "sysenv: performance CPU_MHZ %d WATCHDOG %d (%d)\n",
		 ( k >= 0 && k < NCPU ) ? cpu_mhz_of[k] : -1, wd, (INT)er);
	tm_putstring((CONST UB *)s);
	if ( er == E_OACV ) {
		tell("機器の設定を変更する権限がありません。");
	} else if ( er < E_OK ) {
		tell("設定した内容が保存できません。");
	}
	perf_tick = PERF_TICKS - PERF_SOON;	/* the state read again once it is taken */
}

LOCAL void open_page( INT sheet )
{
	switch ( sheet ) {
	case S_D_SCREEN:	fill_screen();	break;
	case S_D_NET:		fill_net();	break;
	case S_D_USB:		fill_usb();	break;
	case S_D_DISK:		fill_disks();	break;
	case S_D_VIDEO:		fill_video();	break;
	case S_D_KBD:		fill_kbd();	break;
	case S_D_SOUND:		fill_sound();	break;
	default:		break;
	}
	ui_show_sheet(&ui, sheet);
}

LOCAL void screen_set( void )
{
	INT	k = ui_value(&ui, P_SCR_LIST) - 1, c = mode_cur - 1;
	char	v[64];

	if ( k < 0 || k >= nmode ) return;
	if ( k == c ) {
		ui_show_sheet(&ui, S_DEVS);
		return;
	}
	/* the size asked for, and the one there is now to go back to */
	snprintf(v, sizeof(v), "%dx%d %dx%d", mode_w[k], mode_h[k],
		 ( c >= 0 ) ? mode_w[c] : mode_w[k], ( c >= 0 ) ? mode_h[c] : mode_h[k]);
	if ( dev_put("VIDEOMODE", v) < E_OK ) {
		tell("設定した内容が保存できません。");
		return;
	}
	if ( ui_ask(&ui, PN_SCREEN, NULL, 0) == B_RESTART ) {
		power(TRUE);
		return;
	}
	ui_show_sheet(&ui, S_DEVS);
}

LOCAL void kbd_set( void )
{
	char	v[8];

	snprintf(v, sizeof(v), "0x%02x", ( ui_value(&ui, P_KBD_TYPE) == 2 ) ? 0x40 : 0x41);
	if ( dev_put("KBTYPE", v) < E_OK ) {
		tell("設定した内容が保存できません。");
		return;
	}
	snprintf(v, sizeof(v), "%d", ( ui_value(&ui, P_KBD_MODE) == 1 ) ? 0 : 1);
	(void)dev_put("KBMODE", v);
	ui_show_sheet(&ui, S_DEVS);
}

/* A picture of the wallpaper box laid now, and kept for the next start */
LOCAL void wall_set( void )
{
	INT	k = ui_value(&ui, P_WALLS) - 1, i;
	char	us[48], v[64];
	CONST char *hex = "0123456789abcdef";

	if ( k < 0 || k >= nwall ) return;
	for ( i = 0; i < 16; i++ ) {
		INT	d = i * 2 + ( i >= 4 ) + ( i >= 6 ) + ( i >= 8 ) + ( i >= 10 );

		us[d] = hex[wall_id[k].b[i] >> 4];
		us[d + 1] = hex[wall_id[k].b[i] & 15];
	}
	us[8] = us[13] = us[18] = us[23] = '-';
	us[36] = 0;
	snprintf(v, sizeof(v), "%s %d", us, ui_value(&ui, P_WALL_MODE) - 1);
	if ( sys_write(OB_SYS_WALL, v) < E_OK ) {
		tell("背景画面データは不正またはサポート範囲外です。");
		return;
	}
	(void)dev_put("WALLPAPER", v);
	wall_cur = k + 1;
	ui_set_now(&ui, P_WALLS, wall_cur, wall_cur);
}

LOCAL void scheme_set( void )
{
	INT	k = ui_value(&ui, P_SCHEMES) - 1;
	char	v[8];

	if ( k < 0 || k >= nscheme ) return;
	snprintf(v, sizeof(v), "%d", k);
	if ( sys_write(OB_SYS_SCHEME, v) < E_OK ) return;
	(void)dev_put("SCHEME", v);
	ui_set_now(&ui, P_SCHEMES, k + 1, k + 1);
}

/* ---------------------------------------------------------------- 〈ウィンドウ〉 */

/*
 * The windows' look is entries of the look table (LK_LOOK..LK_OPA_WHOLE)
 * written with wm_set_look: the window layer takes each at once, on
 * every window with a frame. What is set is kept as the machine's
 * settings' lines that cf_look_items names (WIN_*): a second after the
 * last change rather than at every step of a volume, and when the
 * window closes.
 *
 * How far a frame is seen through is shown as a percentage, up to
 * ninety -- a frame not there at all is not a frame; the strengths and
 * the shadow's darkness as percentages of their 255.
 */
#define LK_N		24
#define LK_KEEP_TICKS	50		/* a second with nothing changed */
#define WASH_ON		72		/* the strength a colour comes with */
#define BAR_ON		110

LOCAL CONST UW	wash_col[3] = { 0x00202838U, 0x00303030U, 0x00F0F0F0U };	/* 青 墨 白 */
LOCAL CONST UW	bar_col[3] = { 0x00405878U, 0x00303030U, 0x00F0F0F0U };

LOCAL INT	lk_was[LK_N];		/* as the window found them, for 取り消し */
LOCAL BOOL	lk_unkept;		/* changed since last kept */
LOCAL INT	lk_quiet;		/* ticks since the last change */

LOCAL INT lk_get( UINT num )
{
	return (INT)wm_look(num);
}

LOCAL void lk_put( UINT num, INT v )
{
	ER	er;

	if ( lk_get(num) == v ) return;
	er = wm_set_look(num, (UW)v);
	if ( er < E_OK ) {
		char	s[80];

		snprintf(s, sizeof(s), "sysenv: look %d = %d refused (%d)\n", (INT)num, v, (INT)er);
		tm_putstring((CONST UB *)s);
		return;
	}
	lk_unkept = TRUE;
	lk_quiet = 0;
}

LOCAL INT pct_of( INT v255 )
{
	return ( v255 * 100 + 127 ) / 255;
}

LOCAL INT v255_of( INT pct )
{
	return ( pct * 255 + 50 ) / 100;
}

LOCAL INT clamp( INT v, INT lo, INT hi )
{
	return ( v < lo ) ? lo : ( v > hi ) ? hi : v;
}

/* A volume's value, in the box after it */
LOCAL void lk_box( INT num, INT v, CONST char *unit )
{
	char	t[32];

	if ( num == P_LK_MOTION ) {
		snprintf(t, sizeof(t), "%d.%02d秒", v / 1000, ( v % 1000 ) / 10);
	} else {
		snprintf(t, sizeof(t), "%d%s", v, unit);
	}
	ui_set_text(&ui, num + 1, t);
}

/* A volume, its left end the least, and its value in the box after it */
LOCAL void lk_vol( INT num, INT v, INT lo, INT hi, CONST char *unit )
{
	ui_set_range(&ui, num, v, hi, lo);
	lk_box(num, v, unit);
}

/* Which of 配色 青 墨 白 a wash stands at, counted from one */
LOCAL INT colour_choice( CONST UW *col, INT c, BOOL scheme )
{
	INT	k;

	if ( scheme ) return 1;
	for ( k = 0; k < 3; k++ ) {
		if ( ( (UW)c & 0x00FFFFFFU ) == col[k] ) return k + 2;
	}
	return 2;
}

/*
 * Which parts can be worked: with the look off nothing but the switch
 * counts, and a wash left to the scheme has no strength. A part is
 * written only when that changes -- every write is a round trip to the
 * window, and a volume worked meanwhile would be written back over.
 */
LOCAL CONST INT	lk_parts[] = { P_LK_OPA, P_LK_WHOLE, P_LK_WASH, P_LK_WASH_STR, P_LK_BAR, P_LK_BAR_STR,
			       P_LK_ROUND, P_LK_SHD_DARK, P_LK_SHD_XY, P_LK_SHD_SIZE, P_LK_BLUR,
			       P_LK_MOTION, P_LK_LIVE };
#define LK_NPART	( (INT)( sizeof(lk_parts) / sizeof(lk_parts[0]) ) )
LOCAL INT	lk_off[LK_NPART];	/* 1 off, 0 on, -1 not yet written */

LOCAL void look_offs( void )
{
	INT	on = ( lk_get(LK_LOOK) != 0 ), from = lk_get(LK_FROM), k, off;

	for ( k = 0; k < LK_NPART; k++ ) {
		off = !on;
		if ( on && lk_parts[k] == P_LK_WASH_STR ) off = ( ( from & 1 ) != 0 );
		if ( on && lk_parts[k] == P_LK_BAR_STR ) off = ( ( from & 2 ) != 0 );
		if ( lk_off[k] != off ) {
			ui_off(&ui, lk_parts[k], off);
			lk_off[k] = off;
		}
	}
}

/* The parts from the look as it is */
LOCAL void look_fill( void )
{
	INT	on = ( lk_get(LK_LOOK) != 0 ), from = lk_get(LK_FROM), solid = lk_get(LK_OPACITY);
	INT	f[2];

	ui_set_value(&ui, P_LK_ON, on);
	lk_vol(P_LK_OPA, ( solid >= 255 ) ? 0 : clamp(( ( 255 - solid ) * 100 + 127 ) / 255, 0, 90), 0, 90, "％");
	ui_set_value(&ui, P_LK_WHOLE, ( lk_get(LK_OPA_WHOLE) != 0 ) ? 2 : 1);
	ui_set_value(&ui, P_LK_WASH, colour_choice(wash_col, lk_get(LK_WASH), ( from & 1 ) != 0));
	lk_vol(P_LK_WASH_STR, pct_of(lk_get(LK_WASH_STR)), 0, 100, "％");
	ui_set_value(&ui, P_LK_BAR, colour_choice(bar_col, lk_get(LK_BAR_WASH), ( from & 2 ) != 0));
	lk_vol(P_LK_BAR_STR, pct_of(lk_get(LK_BAR_STR)), 0, 100, "％");
	lk_vol(P_LK_ROUND, clamp(lk_get(LK_ROUND), 0, 16), 0, 16, "ドット");
	lk_vol(P_LK_SHD_DARK, pct_of(lk_get(LK_SHD_DARK)), 0, 100, "％");
	f[0] = clamp(lk_get(LK_SHD_DX), 0, 16);
	f[1] = clamp(lk_get(LK_SHD_DY), 0, 16);
	ui_set_fields(&ui, P_LK_SHD_XY, f, 2);
	lk_vol(P_LK_SHD_SIZE, clamp(lk_get(LK_SHD_SIZE), 0, 16), 0, 16, "ドット");
	lk_vol(P_LK_BLUR, clamp(lk_get(LK_BLUR), 0, 16), 0, 16, "ドット");
	lk_vol(P_LK_MOTION, clamp(lk_get(LK_MOTION), 0, 1000), 0, 1000, "");
	ui_set_value(&ui, P_LK_LIVE, ( lk_get(LK_LIVE) != 0 ) ? 1 : 2);
	ui_set_value(&ui, P_LK_GPU, lk_get(LK_GPU) != 0);
	look_offs();
}

/* As the window found it, kept for 取り消し */
LOCAL void look_start( void )
{
	INT	i;

	for ( i = 0; i < cf_look_nitem && i < LK_N; i++ ) lk_was[i] = lk_get(cf_look_items[i].look);
	for ( i = 0; i < LK_NPART; i++ ) lk_off[i] = -1;
	lk_unkept = FALSE;
	look_fill();
}

/* All of it at once: what nothing says (初期値), or what there was (取り消し) */
LOCAL void look_all( BOOL dflt )
{
	INT	i;

	for ( i = 0; i < cf_look_nitem && i < LK_N; i++ ) {
		lk_put(cf_look_items[i].look, dflt ? cf_look_items[i].dflt : lk_was[i]);
	}
	look_fill();
}

/* What is set, into the machine's settings: one write for all the lines */
LOCAL void look_keep( void )
{
	TS_UUID	u;
	ID	k;
	SZ	asz = 0;
	INT	len, i;
	char	v[16];
	ER	er;

	if ( !lk_unkept ) return;
	lk_unkept = FALSE;
	if ( ui_uuid(SYSDEF_CONF_DEV, &u) < E_OK ) return;
	k = ob_opn_obj(&u, OB_OP_R | OB_OP_WRITE);
	if ( k < E_OK ) return;
	er = ob_rea_rec(k, 1, 0, text, TEXT_MAX, &asz);
	len = ( er >= E_OK ) ? (INT)asz : 0;
	for ( i = 0; i < cf_look_nitem && len >= 0; i++ ) {
		CONST CF_ITEM	*it = &cf_look_items[i];
		INT		n = lk_get(it->look);

		if ( it->look == LK_WASH || it->look == LK_BAR_WASH ) {
			snprintf(v, sizeof(v), "0x%06x", (UINT)n & 0x00FFFFFFU);
		} else {
			snprintf(v, sizeof(v), "%d", n);
		}
		len = cf_put(text, len, TEXT_MAX, it->key, 0, v);
	}
	er = ( len < 0 ) ? E_LIMIT : ob_wri_rec(k, 1, 0, text, len, &asz);
	if ( er >= E_OK ) er = ob_trn_rec(k, 1, (UD)len);
	ob_cls_obj(k);
	if ( er < E_OK ) tell("設定した内容が保存できません。");
}

/* A wash chosen: the scheme's colours, or one of the three with a strength that shows */
LOCAL void look_colour( BOOL bar )
{
	INT	c = ui_value(&ui, bar ? P_LK_BAR : P_LK_WASH);
	INT	bit = bar ? 2 : 1, from = lk_get(LK_FROM);
	UINT	str = bar ? LK_BAR_STR : LK_WASH_STR;

	if ( c <= 1 ) {
		lk_put(LK_FROM, from | bit);
		return;
	}
	if ( c > 4 ) c = 4;
	lk_put(bar ? LK_BAR_WASH : LK_WASH, (INT)( bar ? bar_col : wash_col )[c - 2]);
	if ( lk_get(str) <= 0 ) lk_put(str, bar ? BAR_ON : WASH_ON);
	lk_put(LK_FROM, from & ~bit);
}

/*
 * A part of 〈ウィンドウ〉 worked: TRUE when it was one. A volume being
 * dragged has only its box written, not its range, which would move the
 * knob under the hand; a choice has the parts it governs shown again.
 */
LOCAL BOOL look_changed( INT id )
{
	INT	v = ui_value(&ui, id), f[2];

	switch ( id ) {
	case P_LK_OPA:
		v = clamp(v, 0, 90);
		lk_put(LK_OPACITY, 255 - v255_of(v));
		lk_box(id, v, "％");
		return TRUE;
	case P_LK_WASH_STR:
	case P_LK_BAR_STR:
	case P_LK_SHD_DARK:
		v = clamp(v, 0, 100);
		lk_put(( id == P_LK_WASH_STR ) ? LK_WASH_STR : ( id == P_LK_BAR_STR ) ? LK_BAR_STR : LK_SHD_DARK,
		       v255_of(v));
		lk_box(id, v, "％");
		return TRUE;
	case P_LK_ROUND:
	case P_LK_SHD_SIZE:
	case P_LK_BLUR:
		v = clamp(v, 0, 16);
		lk_put(( id == P_LK_ROUND ) ? LK_ROUND : ( id == P_LK_SHD_SIZE ) ? LK_SHD_SIZE : LK_BLUR, v);
		lk_box(id, v, "ドット");
		return TRUE;
	case P_LK_MOTION:
		v = ( clamp(v, 0, 1000) + 25 ) / 50 * 50;
		lk_put(LK_MOTION, v);
		lk_box(id, v, "");
		return TRUE;
	case P_LK_SHD_XY:
		(void)ui_fields(&ui, P_LK_SHD_XY, f, 2);
		lk_put(LK_SHD_DX, clamp(f[0], 0, 16));
		lk_put(LK_SHD_DY, clamp(f[1], 0, 16));
		return TRUE;
	case P_LK_WASH:
	case P_LK_BAR:
		look_colour(id == P_LK_BAR);
		id = ( id == P_LK_BAR ) ? P_LK_BAR_STR : P_LK_WASH_STR;
		lk_vol(id, pct_of(lk_get(( id == P_LK_BAR_STR ) ? LK_BAR_STR : LK_WASH_STR)), 0, 100, "％");
		break;
	case P_LK_ON:		lk_put(LK_LOOK, v != 0);		break;
	case P_LK_WHOLE:	lk_put(LK_OPA_WHOLE, v == 2);		break;
	case P_LK_LIVE:		lk_put(LK_LIVE, v == 1);		break;
	case P_LK_GPU:		lk_put(LK_GPU, v != 0);			break;
	default:		return FALSE;
	}
	look_offs();
	return TRUE;
}

/* ---------------------------------------------------------------- the programs */

/*
 * A request to the desktop (DT_RQ_INSTALL, DT_RQ_REMOVE, DT_RQ_FONT_*,
 * DT_RQ_OPEN, DT_RQ_CLOSE), waited for: what it answered, and the answer
 * itself. flags and text are the request's (text NULL: none).
 */
LOCAL ER desk_req( UINT req, CONST TS_UUID *target, INT recno, UINT flags, CONST char *text, T_DTANS *an )
{
	static T_DTREQ	rq;
	T_OBCRE	c;
	TS_UUID	d, reply;
	SZ	asz = 0;
	ID	k, ka;
	INT	i;
	ER	er;

	memset(an, 0, sizeof(*an));
	if ( ob_fnd_nam((CONST UB *)DT_REQ_NAME, &d) < E_OK ) return E_NOEXS;
	memset(&c, 0, sizeof(c));
	c.type = OB_T_CHANNEL;
	if ( ob_cre_obj(&c, &reply) < E_OK ) return E_NOMEM;
	ka = ob_opn_obj(&reply, OB_OP_READ | OB_O_NOWAIT);
	if ( ka <= 0 ) {
		(void)ob_del_obj(&reply);
		return E_OBJ;
	}
	memset(&rq, 0, sizeof(rq));
	rq.req = req;
	rq.seq = 1;
	rq.target = *target;
	rq.recno = recno;
	rq.reply = reply;
	rq.win = ( ask_win != NULL ) ? *ask_win : ui.win;	/* the window that asks */
	rq.flags = flags;
	if ( text != NULL ) strncpy((char *)rq.text, text, sizeof(rq.text) - 1);
	k = ob_opn_obj(&d, OB_OP_WRITE);
	er = ( k > 0 ) ? ob_wri_rec(k, 0, 0, &rq, sizeof(rq), &asz) : (ER)k;
	if ( k > 0 ) ob_cls_obj(k);
	/* long enough for the person to type the administrator's password */
	for ( i = 0; er >= E_OK && i < 30000; i++ ) {
		if ( ob_rea_rec(ka, 0, 0, an, sizeof(*an), &asz) >= E_OK && asz == (SZ)sizeof(*an)
		  && an->seq == rq.seq ) {
			er = an->er;
			break;
		}
		(void)tk_dly_tsk(20);
	}
	if ( i >= 30000 ) er = E_TMOUT;
	ob_cls_obj(ka);
	(void)ob_del_obj(&reply);
	return er;
}

LOCAL ER desk_ask( UINT req, CONST TS_UUID *target, INT recno, T_DTANS *an )
{
	return desk_req(req, target, recno, 0, NULL, an);
}

/* A question with two buttons (panel 11): TRUE for the one that goes ahead */
LOCAL BOOL confirm( CONST char *l1, CONST char *l2, CONST char *yes )
{
	CONST char *l[3];

	l[0] = l1;
	l[1] = l2;
	l[2] = yes;
	return (BOOL)( ui_ask(&ui, PN_POWER, l, 3) == B_POWER );
}

LOCAL void prog_detail( void )
{
	INT		k = ui_value(&ui, P_PROGS) - 1;
	PROGINFO	*pi;
	char		l[6][96], us[40];
	CONST char	*lp[6];
	INT		i;

	if ( k < 0 || k >= nprog ) {
		tell("アプリケーションを選んでください。");
		return;
	}
	pi = &prog_info[k];
	for ( i = 0; i < 16; i++ ) {
		snprintf(us + i * 2 + ( i >= 4 ) + ( i >= 6 ) + ( i >= 8 ) + ( i >= 10 ), 3, "%02x",
			 prog_id[k].b[i]);
	}
	us[8] = us[13] = us[18] = us[23] = '-';
	us[36] = 0;
	snprintf(l[0], sizeof(l[0]), "名前：%s", pi->name);
	snprintf(l[1], sizeof(l[1]), "バージョン：%s", ( pi->version[0] != 0 ) ? pi->version : "（なし）");
	snprintf(l[2], sizeof(l[2]), "識別子：%s", pi->id);
	snprintf(l[3], sizeof(l[3]), "種類：%s",
		 ( strcmp(pi->kind, "accessory") == 0 ) ? "小物"
		 : ( strcmp(pi->kind, "utility") == 0 ) ? "道具" : "アプリケーション");
	snprintf(l[4], sizeof(l[4]), "実身：%s", us);
	snprintf(l[5], sizeof(l[5]), "%s", pi->builtin ? "システムに組み込み（削除できません）" : "追加したもの（削除できます）");
	for ( i = 0; i < 6; i++ ) lp[i] = l[i];
	(void)ui_ask(&ui, PN_PROG, lp, 6);
}

LOCAL void prog_delete( void )
{
	INT		k = ui_value(&ui, P_PROGS) - 1;
	T_DTANS		an;
	char		l1[128], s[128];
	ER		er;

	if ( ui_sheet(&ui) != S_VERSION || k < 0 || k >= nprog ) {
		tell("削除するアプリケーションを選んでください。");
		return;
	}
	if ( prog_info[k].builtin ) {
		tell("システムに組み込みのアプリケーションは削除できません。");
		return;
	}
	snprintf(l1, sizeof(l1), "〈%s %s〉を削除します。", prog_info[k].name, prog_info[k].version);
	if ( !confirm(l1, "削除すると元に戻せません。", "削除") ) return;
	er = desk_ask(DT_RQ_REMOVE, &prog_id[k], 0, &an);
	snprintf(s, sizeof(s), "sysenv: remove %s %d\n", prog_info[k].id, (INT)er);
	tm_putstring((CONST UB *)s);
	if ( er == E_OACV ) {
		tell("アプリケーションを削除する権限がありません。");
	} else if ( er < E_OK && er != E_ABORT ) {
		tell("削除できませんでした。");
	}
	fill_version();
}

/* A package dropped on the list: installed, or the program it is a newer version of updated */
LOCAL void prog_drop( CONST T_OBDROP *d )
{
	T_DTANS		an;
	char		s[160];
	INT		i, done = 0;
	ER		er;

	for ( i = 0; i < d->n && i < OB_DROP_MAX; i++ ) {
		er = desk_ask(DT_RQ_INSTALL, &d->v[i].target, 0, &an);
		if ( er == E_LIMIT ) {
			if ( !confirm("同じか、より新しいバージョンがすでにあります。",
				      "置き換えますか？", "置き換え") ) continue;
			er = desk_ask(DT_RQ_INSTALL, &d->v[i].target, 1, &an);
		}
		snprintf(s, sizeof(s), "sysenv: install %d how %d\n", (INT)er, (INT)an.how);
		tm_putstring((CONST UB *)s);
		if ( er == E_OBJ ) {
			tell("アプリケーションのパッケージではありません。");
		} else if ( er == E_PAR ) {
			tell("システムに組み込みのアプリケーションは置き換えられません。");
		} else if ( er == E_OACV ) {
			tell("アプリケーションをインストールする権限がありません。");
		} else if ( er == E_ABORT ) {
			continue;		/* the administrator's password was not given */
		} else if ( er < E_OK ) {
			tell("インストールできませんでした。");
		} else {
			done++;
		}
	}
	fill_version();
	if ( done > 0 ) {
		tell(( an.how == DT_HOW_UPDATED ) ? "アプリケーションを更新しました。"
						  : "アプリケーションをインストールしました。");
	}
}

/* ---------------------------------------------------------------- 〈書体〉 */

/*
 * The faces of the 書体箱 (design 10.3). A face object is one whose data
 * record is a font file: OpenType, TrueType, or a collection of them
 * (ttcf) whose faces are counted from 0. What each face is registered
 * as -- 詳細名, used or not, the family documents name it by, its class,
 * its attributes, its base family -- is the JSON of the object's
 * resource DT_FONT_RES; a face it does not name is as its file says.
 * The list shows one line a face. Whatever changes the box or what a
 * face is registered as is asked of the desktop (DT_RQ_FONT_*), which
 * opens the faces and draws with them.
 *
 * What a face's file says is read here from the file itself, a table
 * at a time: its names (the 'name' table), the letters it has ('cmap'),
 * whether it is bold or italic ('OS/2', 'head'), whether its letters
 * differ in width ('post') and whether it has forms for vertical writing
 * ('vert' of 'GSUB').
 */
#define FACE_MAX	8		/* faces of one file worked with */
#define FROW_MAX	32		/* lines of the list */
#define FNAME_MAX	64

typedef struct {
	char	name[FNAME_MAX];	/* 詳細名 */
	BOOL	use;
	char	family[FNAME_MAX];
	UINT	cls, attr;		/* FN_FA_* bits, as fn.h numbers them */
	char	base[FNAME_MAX];
} FSET;

/* the attributes, as include/ts/fn.h numbers them */
#define FA_BOLD		0x0001
#define FA_ITALIC	0x0002
#define FA_BAG		0x0004
#define FA_SHADOW	0x0008
#define FA_PROP		0x0010
#define FA_VERT		0x0020
#define FA_LOOK		0x000F
#define CLS_SERIF	0x6000U

typedef struct {
	TS_UUID	obj;
	INT	index;
	FSET	s;
} FROW;

LOCAL FROW	frow[FROW_MAX];
LOCAL INT	nfrow;
LOCAL char	font_line[FROW_MAX][FNAME_MAX + 24];

/* ------------------------------------------------ a face's file, read as it is */

#define SF_TABLES	48

typedef struct {
	ID	key;
	INT	rec;			/* the record that is the file */
	INT	nface;
	INT	ntab;
	UW	tag[SF_TABLES], off[SF_TABLES], len[SF_TABLES];
	UD	size;			/* bytes of the whole object */
} SFNT;

LOCAL UW be16( CONST UB *p )
{
	return ( (UW)p[0] << 8 ) | p[1];
}

LOCAL UW be32( CONST UB *p )
{
	return ( (UW)p[0] << 24 ) | ( (UW)p[1] << 16 ) | ( (UW)p[2] << 8 ) | p[3];
}

#define TAG(a, b, c, d)	( ( (UW)(a) << 24 ) | ( (UW)(b) << 16 ) | ( (UW)(c) << 8 ) | (UW)(d) )

LOCAL BOOL sf_read( SFNT *sf, UW off, void *buf, SZ n )
{
	SZ	asz = 0;

	return (BOOL)( ob_rea_rec(sf->key, sf->rec, (D)off, buf, n, &asz) >= E_OK && asz == n );
}

LOCAL BOOL sf_magic( UW m )
{
	return (BOOL)( m == 0x00010000U || m == TAG('O', 'T', 'T', 'O') || m == TAG('t', 'r', 'u', 'e')
		    || m == TAG('t', 't', 'c', 'f') );
}

LOCAL void sf_close( SFNT *sf )
{
	if ( sf->key > 0 ) ob_cls_obj(sf->key);
	sf->key = 0;
}

/* Face 'index' of an object's font file: E_OK, its tables known */
LOCAL ER sf_open( CONST TS_UUID *u, INT index, SFNT *sf )
{
	T_OBREC	rec[16];
	T_OBREF	r;
	UB	h[16], d[16];
	INT	cnt = 0, i;
	UW	dir = 0;

	memset(sf, 0, sizeof(*sf));
	sf->rec = -1;
	if ( ob_ref_obj(u, &r) >= E_OK ) sf->size = r.size;
	sf->key = ob_opn_obj(u, OB_OP_R);
	if ( sf->key <= 0 ) {
		sf->key = 0;
		return E_NOEXS;
	}
	if ( ob_lst_rec(sf->key, rec, 16, &cnt) >= E_OK ) {
		for ( i = 0; i < cnt && i < 16 && sf->rec < 0; i++ ) {
			if ( rec[i].rt == OB_RT_FONT ) sf->rec = rec[i].recno;
		}
		for ( i = 0; i < cnt && i < 16 && sf->rec < 0; i++ ) {
			sf->rec = rec[i].recno;
			if ( !sf_read(sf, 0, h, 4) || !sf_magic(be32(h)) ) sf->rec = -1;
		}
	}
	if ( sf->rec < 0 || !sf_read(sf, 0, h, 16) || !sf_magic(be32(h)) ) {
		sf_close(sf);
		return E_OBJ;
	}
	sf->nface = 1;
	if ( be32(h) == TAG('t', 't', 'c', 'f') ) {
		sf->nface = (INT)be32(h + 8);
		if ( sf->nface < 1 ) sf->nface = 1;
		if ( index >= sf->nface || !sf_read(sf, 12 + 4 * (UW)index, d, 4) ) {
			sf_close(sf);
			return E_PAR;
		}
		dir = be32(d);
	} else if ( index != 0 ) {
		sf_close(sf);
		return E_PAR;
	}
	if ( !sf_read(sf, dir, d, 12) ) {
		sf_close(sf);
		return E_OBJ;
	}
	sf->ntab = (INT)be16(d + 4);
	if ( sf->ntab > SF_TABLES ) sf->ntab = SF_TABLES;
	for ( i = 0; i < sf->ntab; i++ ) {
		if ( !sf_read(sf, dir + 12 + 16 * (UW)i, d, 16) ) break;
		sf->tag[i] = be32(d);
		sf->off[i] = be32(d + 8);
		sf->len[i] = be32(d + 12);
	}
	sf->ntab = i;
	return E_OK;
}

/* A table of the face read whole (at most max bytes); NULL when it has none. The caller frees it */
LOCAL UB *sf_table( SFNT *sf, UW tag, UW max, UW *p_len )
{
	INT	i;
	UB	*b;
	UW	n;

	for ( i = 0; i < sf->ntab && sf->tag[i] != tag; i++ ) ;
	if ( i == sf->ntab || sf->len[i] == 0 ) return NULL;
	n = ( sf->len[i] < max ) ? sf->len[i] : max;
	b = malloc(n);
	if ( b == NULL ) return NULL;
	if ( !sf_read(sf, sf->off[i], b, n) ) {
		free(b);
		return NULL;
	}
	*p_len = n;
	return b;
}

/* UTF-16 written high byte first, as UTF-8 */
LOCAL void utf16_to8( CONST UB *s, UW n, char *out, INT max )
{
	INT	k = 0;
	UW	i;

	for ( i = 0; i + 1 < n && k < max - 4; i += 2 ) {
		UW	c = be16(s + i);

		if ( c >= 0xD800 && c < 0xDC00 && i + 3 < n ) {
			UW	lo = be16(s + i + 2);

			if ( lo >= 0xDC00 && lo < 0xE000 ) {
				c = 0x10000 + ( ( c - 0xD800 ) << 10 ) + ( lo - 0xDC00 );
				i += 2;
			}
		}
		if ( c < 0x20 ) continue;
		if ( c < 0x80 ) {
			out[k++] = (char)c;
		} else if ( c < 0x800 ) {
			out[k++] = (char)( 0xC0 | ( c >> 6 ) );
			out[k++] = (char)( 0x80 | ( c & 0x3F ) );
		} else if ( c < 0x10000 ) {
			out[k++] = (char)( 0xE0 | ( c >> 12 ) );
			out[k++] = (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
			out[k++] = (char)( 0x80 | ( c & 0x3F ) );
		} else {
			out[k++] = (char)( 0xF0 | ( c >> 18 ) );
			out[k++] = (char)( 0x80 | ( ( c >> 12 ) & 0x3F ) );
			out[k++] = (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
			out[k++] = (char)( 0x80 | ( c & 0x3F ) );
		}
	}
	out[k] = 0;
}

/*
 * One of the face's names (the 'name' table's nameID): Japanese when it
 * has it in Japanese, else English, else in whatever it has; "" none.
 */
LOCAL void sf_name( CONST UB *nt, UW len, UW id, BOOL japanese, char *out, INT max )
{
	UW	n, so, i, best = 0xFFFF;
	INT	rank = 0;

	out[0] = 0;
	if ( nt == NULL || len < 6 ) return;
	n = be16(nt + 2);
	so = be16(nt + 4);
	for ( i = 0; i < n && 6 + 12 * i + 12 <= len; i++ ) {
		CONST UB *r = nt + 6 + 12 * i;
		UW	pl = be16(r), en = be16(r + 2), lang = be16(r + 4);
		INT	k = 0;

		if ( be16(r + 6) != id ) continue;
		if ( pl == 3 && ( en == 1 || en == 10 ) ) {
			k = ( japanese && lang == 0x0411 ) ? 4 : ( lang == 0x0409 ) ? 3 : 2;
		} else if ( pl == 0 ) {
			k = 2;
		} else if ( pl == 1 && en == 0 ) {
			k = 1;
		}
		if ( k > rank ) {
			rank = k;
			best = i;
		}
	}
	if ( rank == 0 ) return;
	{
		CONST UB *r = nt + 6 + 12 * best;
		UW	l = be16(r + 8), o = so + be16(r + 10);

		if ( o + l > len ) return;
		if ( rank == 1 ) {
			INT	k;

			for ( k = 0; k < (INT)l && k < max - 1; k++ ) out[k] = ( nt[o + k] < 0x80 ) ? (char)nt[o + k] : '?';
			out[k] = 0;
		} else {
			utf16_to8(nt + o, l, out, max);
		}
	}
}

/* What the face's data says of it */
typedef struct {
	char	family[FNAME_MAX];	/* its family (16, else 1) */
	char	full[FNAME_MAX];	/* its full name (4) */
	UINT	attr;			/* FA_BOLD FA_ITALIC FA_PROP FA_VERT as the data says */
	UINT	cls;			/* a class guessed from what it says of its kind */
	UW	lo, hi;			/* the codes it has letters for */
	UW	count;
} SFINFO;

/* The codes a cmap subtable maps (format 4 or 12) */
LOCAL void cmap_range( CONST UB *t, UW len, SFINFO *si )
{
	UW	n, i, best = 0, fmt = 0, sub;
	INT	rank = 0;

	if ( len < 4 ) return;
	n = be16(t + 2);
	for ( i = 0; i < n && 4 + 8 * i + 8 <= len; i++ ) {
		UW	pl = be16(t + 4 + 8 * i), en = be16(t + 6 + 8 * i), o = be32(t + 8 + 8 * i);
		INT	k = 0;

		if ( o + 4 > len ) continue;
		if ( ( pl == 3 && en == 10 ) || ( pl == 0 && ( en == 4 || en == 6 ) ) ) k = 3;
		else if ( ( pl == 3 && en == 1 ) || pl == 0 ) k = 2;
		if ( k > rank && ( be16(t + o) == 4 || be16(t + o) == 12 ) ) {
			rank = k;
			best = o;
		}
	}
	if ( rank == 0 ) return;
	sub = best;
	fmt = be16(t + sub);
	si->lo = 0xFFFFFFFFU;
	if ( fmt == 12 && sub + 16 <= len ) {
		UW	ng = be32(t + sub + 12);

		for ( i = 0; i < ng && sub + 16 + 12 * i + 12 <= len; i++ ) {
			UW	a = be32(t + sub + 16 + 12 * i), b = be32(t + sub + 20 + 12 * i);

			if ( b < a ) continue;
			if ( a < si->lo ) si->lo = a;
			if ( b > si->hi ) si->hi = b;
			si->count += b - a + 1;
		}
	} else if ( fmt == 4 && sub + 14 <= len ) {
		UW	seg = be16(t + sub + 6) / 2;

		for ( i = 0; i < seg; i++ ) {
			UW	eo = sub + 14 + 2 * i, so = sub + 16 + 2 * seg + 2 * i;
			UW	a, b;

			if ( so + 2 > len ) break;
			a = be16(t + so);
			b = be16(t + eo);
			if ( a == 0xFFFF || b < a ) continue;
			if ( a < si->lo ) si->lo = a;
			if ( b > si->hi ) si->hi = b;
			si->count += b - a + 1;
		}
	}
	if ( si->count == 0 ) si->lo = 0;
}

/* Whether the face's substitutions have the vertical forms ('vert' or 'vrt2') */
LOCAL BOOL has_vert( CONST UB *g, UW len )
{
	UW	fl, n, i;

	if ( g == NULL || len < 10 ) return FALSE;
	fl = be16(g + 6);
	if ( fl + 2 > len ) return FALSE;
	n = be16(g + fl);
	for ( i = 0; i < n && fl + 2 + 6 * i + 6 <= len; i++ ) {
		UW	t = be32(g + fl + 2 + 6 * i);

		if ( t == TAG('v', 'e', 'r', 't') || t == TAG('v', 'r', 't', '2') ) return TRUE;
	}
	return FALSE;
}

LOCAL void sf_info( SFNT *sf, SFINFO *si )
{
	UB	*t;
	UW	len = 0;

	memset(si, 0, sizeof(*si));
	t = sf_table(sf, TAG('n', 'a', 'm', 'e'), 256 * 1024, &len);
	if ( t != NULL ) {
		sf_name(t, len, 16, FALSE, si->family, sizeof(si->family));
		if ( si->family[0] == 0 ) sf_name(t, len, 1, FALSE, si->family, sizeof(si->family));
		sf_name(t, len, 4, TRUE, si->full, sizeof(si->full));
		free(t);
	}
	if ( si->full[0] == 0 ) snprintf(si->full, sizeof(si->full), "%s", si->family);
	t = sf_table(sf, TAG('c', 'm', 'a', 'p'), 4 * 1024 * 1024, &len);
	if ( t != NULL ) {
		cmap_range(t, len, si);
		free(t);
	}
	si->attr = FA_PROP;
	t = sf_table(sf, TAG('O', 'S', '/', '2'), 96, &len);
	if ( t != NULL ) {
		if ( len >= 64 ) {
			UW	sel = be16(t + 62), fam = be16(t + 30) >> 8;

			if ( ( sel & 0x01 ) != 0 ) si->attr |= FA_ITALIC;
			if ( ( sel & 0x20 ) != 0 || be16(t + 4) >= 600 ) si->attr |= FA_BOLD;
			/* the kind it says it is: the serif kinds, the sans, the script */
			if ( ( fam >= 1 && fam <= 5 ) || fam == 7 ) si->cls = CLS_SERIF | 0x1000U;
			else if ( fam == 8 ) si->cls = 0x0040U;
			else if ( fam == 10 ) si->cls = 0x0100U;
			if ( len >= 82 && ( be32(t + 78) & ( 1U << 17 ) ) != 0 && si->cls != 0 ) si->cls |= 0x0010U;
		}
		free(t);
	}
	t = sf_table(sf, TAG('p', 'o', 's', 't'), 16, &len);
	if ( t != NULL ) {
		if ( len >= 16 && be32(t + 12) != 0 ) si->attr &= ~FA_PROP;
		free(t);
	}
	t = sf_table(sf, TAG('G', 'S', 'U', 'B'), 64 * 1024, &len);
	if ( t != NULL ) {
		if ( has_vert(t, len) ) si->attr |= FA_VERT;
		free(t);
	}
}

/* ------------------------------------------------ the settings, as the resource keeps them */

LOCAL UINT hex4( CONST char *s )
{
	UINT	v = 0;
	INT	i;

	for ( i = 0; s[i] != 0 && i < 8; i++ ) {
		char	c = s[i];

		if ( c >= '0' && c <= '9' )      v = ( v << 4 ) | (UINT)( c - '0' );
		else if ( c >= 'a' && c <= 'f' ) v = ( v << 4 ) | (UINT)( c - 'a' + 10 );
		else if ( c >= 'A' && c <= 'F' ) v = ( v << 4 ) | (UINT)( c - 'A' + 10 );
		else if ( c == 'x' || c == 'X' ) v = 0;
		else if ( c != ' ' ) return 0x10000U;	/* not a number */
	}
	return v;
}

/* An object's faces' settings as its resource has them: how many, and each one's index */
LOCAL INT faces_said( CONST TS_UUID *u, FSET *fs, INT *idx, INT max )
{
	static UB	raw[4096];
	T_JSON		root, list, it;
	SZ		asz = 0;
	ID		key;
	INT		n = 0;

	key = ob_opn_obj(u, OB_OP_R);
	if ( key <= 0 ) return 0;
	if ( ob_rea_res(key, (CONST UB *)DT_FONT_RES, 0, raw, sizeof(raw) - 1, &asz) >= E_OK && asz > 0
	  && js_parse(raw, (INT)asz, &root) >= E_OK && js_get(&root, "faces", &list) >= E_OK ) {
		for ( it.s = NULL; n < max && js_next(&list, &it); ) {
			char	h[16];

			memset(&fs[n], 0, sizeof(fs[n]));
			idx[n] = (INT)js_get_num(&it, "index", 0);
			fs[n].use = js_get_bool(&it, "use", TRUE);
			(void)js_get_str(&it, "name", (UB *)fs[n].name, sizeof(fs[n].name));
			(void)js_get_str(&it, "family", (UB *)fs[n].family, sizeof(fs[n].family));
			(void)js_get_str(&it, "base", (UB *)fs[n].base, sizeof(fs[n].base));
			if ( js_get_str(&it, "class", (UB *)h, sizeof(h)) > 0 ) fs[n].cls = hex4(h) & 0xFFFFU;
			if ( js_get_str(&it, "attr", (UB *)h, sizeof(h)) > 0 ) fs[n].attr = hex4(h) & 0xFFFFU;
			n++;
		}
	}
	ob_cls_obj(key);
	return n;
}

/* Whether an object has settings of its own (a face saved, 書体を保存する) */
LOCAL BOOL has_settings( CONST TS_UUID *u )
{
	FSET	fs[1];
	INT	idx[1];

	return (BOOL)( faces_said(u, fs, idx, 1) > 0 );
}

/* A string into JSON, in its quotes */
LOCAL void js_put_str( char *b, INT max, CONST char *s )
{
	INT	k = (INT)strlen(b);

	if ( k < max - 1 ) b[k++] = '"';
	for ( ; *s != 0 && k < max - 3; s++ ) {
		if ( *s == '"' || *s == '\\' ) b[k++] = '\\';
		if ( (UB)*s >= 0x20 ) b[k++] = *s;
	}
	if ( k < max - 1 ) b[k++] = '"';
	b[k] = 0;
}

/* One face's settings as DT_RQ_FONT_REG and DT_RQ_FONT_SET take them */
LOCAL void fset_json( CONST FSET *f, char *b, INT max )
{
	char	t[64];

	snprintf(b, max, "{\"name\":");
	js_put_str(b, max, f->name);
	snprintf(t, sizeof(t), ",\"use\":%s,\"family\":", f->use ? "true" : "false");
	strncat(b, t, max - strlen(b) - 1);
	js_put_str(b, max, f->family);
	snprintf(t, sizeof(t), ",\"class\":\"%04x\",\"attr\":\"%04x\",\"base\":", f->cls & 0xFFFFU, f->attr & 0xFFFFU);
	strncat(b, t, max - strlen(b) - 1);
	js_put_str(b, max, f->base);
	strncat(b, "}", max - strlen(b) - 1);
}

/*
 * A face's settings: as its object's resource says, the rest as its file
 * says -- its full name for 詳細名, its family, the class its kind
 * suggests, its looks.
 */
LOCAL void fset_of( CONST SFINFO *si, CONST FSET *said, FSET *f )
{
	if ( said != NULL ) {
		*f = *said;
	} else {
		memset(f, 0, sizeof(*f));
		f->use = TRUE;
		f->cls = si->cls;
		f->attr = si->attr;
	}
	if ( f->name[0] == 0 ) snprintf(f->name, sizeof(f->name), "%s", si->full);
	if ( f->family[0] == 0 ) snprintf(f->family, sizeof(f->family), "%s", si->family);
	/* what the data says is the data's */
	f->attr = ( f->attr & ~( FA_PROP | FA_VERT ) ) | ( si->attr & ( FA_PROP | FA_VERT ) );
}

/* ------------------------------------------------ the list */

LOCAL BOOL same_obj( CONST TS_UUID *a, CONST TS_UUID *b )
{
	return (BOOL)( memcmp(a, b, sizeof(*a)) == 0 );
}

LOCAL void fill_fonts( void )
{
	static TS_UUID	ids[LIST_MAX];
	static char	nm[LIST_MAX][64];
	FSET		said[FACE_MAX];
	INT		idx[FACE_MAX], n, i, k, ns;
	CONST char	*lp[FROW_MAX];
	SFNT		sf;
	SFINFO		si;

	n = box_links(SYSDEF_FONT_BOX, ids, nm, LIST_MAX);
	nfrow = 0;
	for ( i = 0; i < n && nfrow < FROW_MAX; i++ ) {
		ns = faces_said(&ids[i], said, idx, FACE_MAX);
		for ( k = 0; k < ( ( ns > 0 ) ? ns : 1 ) && nfrow < FROW_MAX; k++ ) {
			INT	index = ( ns > 0 ) ? idx[k] : 0;
			FROW	*r = &frow[nfrow];

			memset(&si, 0, sizeof(si));
			if ( sf_open(&ids[i], index, &sf) >= E_OK ) {
				sf_info(&sf, &si);
				sf_close(&sf);
			}
			if ( si.full[0] == 0 ) snprintf(si.full, sizeof(si.full), "%s", nm[i]);
			r->obj = ids[i];
			r->index = index;
			fset_of(&si, ( ns > 0 ) ? &said[k] : NULL, &r->s);
			snprintf(font_line[nfrow], sizeof(font_line[nfrow]), "%s\t%s", r->s.name,
				 r->s.use ? "使用する" : "使用しない");
			lp[nfrow] = font_line[nfrow];
			nfrow++;
		}
	}
	ui_names(&ui, P_FONTS, lp, nfrow);
	ui_set_value(&ui, P_FONTS, 0);
	ui_set_value(&ui, P_FONT_USE, 0);
	ui_off(&ui, P_FONT_USE, TRUE);
	{
		char	s[48];

		snprintf(s, sizeof(s), "sysenv: fonts %d\n", nfrow);
		tm_putstring((CONST UB *)s);
	}
}

/* A line of the list chosen: whether it is used shows */
LOCAL void font_chosen( void )
{
	INT	k = ui_value(&ui, P_FONTS) - 1;

	if ( k < 0 || k >= nfrow ) {
		ui_off(&ui, P_FONT_USE, TRUE);
		return;
	}
	ui_off(&ui, P_FONT_USE, FALSE);
	ui_set_value(&ui, P_FONT_USE, frow[k].s.use ? 1 : 2);
}

/* 使用する／使用しない: taken at once */
LOCAL void font_use( void )
{
	INT	k = ui_value(&ui, P_FONTS) - 1, keep = k + 1;
	BOOL	on = ( ui_value(&ui, P_FONT_USE) != 2 );
	T_DTANS	an;
	char	s[128];
	ER	er;

	if ( k < 0 || k >= nfrow || on == frow[k].s.use ) return;
	er = desk_req(DT_RQ_FONT_USE, &frow[k].obj, frow[k].index, on ? 0 : DT_F_FONT_OFF, NULL, &an);
	snprintf(s, sizeof(s), "sysenv: font use %s %d (%d)\n", frow[k].s.name, (INT)on, (INT)er);
	fill_fonts();
	ui_set_value(&ui, P_FONTS, keep);
	font_chosen();
	tm_putstring((CONST UB *)s);		/* told once the list shows it */
	if ( er == E_RONLY ) {
		tell("システムが使う書体は、使用しないにできません。");
	} else if ( er < E_OK ) {
		tell("書体の設定を変更できませんでした。");
	}
}

/* The line chosen, or -1 told why there is none */
LOCAL INT font_pick( CONST char *what )
{
	INT	k = ui_value(&ui, P_FONTS) - 1;

	if ( ui_sheet(&ui) != S_FONT || k < 0 || k >= nfrow ) {
		tell(what);
		return -1;
	}
	return k;
}

/* 保存: the face's object copied as a real object, where the desktop asks */
LOCAL void font_save( void )
{
	INT	k = font_pick("保存する書体を選んでください。");
	T_DTANS	an;
	char	s[96];
	ER	er;

	if ( k < 0 ) return;
	er = desk_req(DT_RQ_FONT_SAVE, &frow[k].obj, frow[k].index, 0, NULL, &an);
	snprintf(s, sizeof(s), "sysenv: font save %d\n", (INT)er);
	tm_putstring((CONST UB *)s);
	if ( er < E_OK && er != E_ABORT ) {
		tell("書体を保存できませんでした。");
	}
}

/* 削除: the face's object taken out of the 書体箱 and deleted */
LOCAL void font_delete( void )
{
	INT	k = font_pick("削除する書体を選んでください。"), i, others = 0;
	CONST char *l[2];
	char	l1[128], l2[128], s[128];
	T_DTANS	an;
	ER	er;

	if ( k < 0 ) return;
	for ( i = 0; i < nfrow; i++ ) {
		if ( i != k && same_obj(&frow[i].obj, &frow[k].obj) ) others++;
	}
	snprintf(l1, sizeof(l1), "〈%s〉を削除します。", frow[k].s.name);
	if ( others > 0 ) {
		snprintf(l2, sizeof(l2), "同じ実身の書体（ほかに%d書体）も削除されます。", others);
	} else {
		snprintf(l2, sizeof(l2), "保存していない書体は、元に戻せません。");
	}
	l[0] = l1;
	l[1] = l2;
	if ( ui_ask(&ui, PN_FONT_DEL, l, 2) != B_FONT_DEL ) return;
	er = desk_req(DT_RQ_FONT_DEL, &frow[k].obj, 0, 0, NULL, &an);
	snprintf(s, sizeof(s), "sysenv: font delete %s (%d)\n", frow[k].s.name, (INT)er);
	fill_fonts();
	tm_putstring((CONST UB *)s);		/* told once the list shows it */
	if ( er == E_RONLY ) {
		tell("システムが使う書体は削除できません。");
	} else if ( er == E_OACV ) {
		tell("書体を削除する権限がありません。");
	} else if ( er < E_OK && er != E_ABORT ) {
		tell("書体を削除できませんでした。");
	}
}

/* ------------------------------------------------ フォント設定 */

/*
 * A window of its own, a normal one, with the face object's faces: what
 * the file is and what each face is registered as, worked with the
 * panel's parts. Opened from 詳細情報 for a face of the box, or for a
 * font file dropped on the list, which closing it registers when asked.
 * 見本表示 and 注釈表示 put a document of the face's letters and of what
 * its file says of itself in a window of the text editor; the documents
 * are the window's, deleted when it closes.
 */
#define FS_FACES	1
#define FS_SIZE		2
#define FS_RANGE	3
#define FS_SAMPLE	4
#define FS_NOTE		5
#define FS_NAME		6
#define FS_USE		7
#define FS_BOLD		8
#define FS_ITALIC	9
#define FS_BAG		10
#define FS_SHADOW	11
#define FS_PROP		12
#define FS_VERT		13
#define FS_DETAIL	14
#define FS_HEX		15
#define FS_FAMS		16
#define FS_FAMDET	17
#define FS_UNIFY	18

/* ファミリ: the panel of the family and the base */
#define FD_NAME		1
#define FD_CLASS	2
#define FD_LIST		3
#define FD_BNAME	4
#define FD_BCLASS	5
#define FD_BLIST	6
#define FD_SET		8

#define FAM_MAX		24

typedef struct {
	BOOL	open;
	UI	w;
	BOOL	reg;			/* a file dropped, to be registered */
	TS_UUID	obj;
	INT	nface, cur;
	FSET	f[FACE_MAX], was[FACE_MAX];
	char	full[FACE_MAX][FNAME_MAX];
	UW	lo[FACE_MAX], hi[FACE_MAX], count[FACE_MAX];
	UD	size;
	BOOL	sample, note;		/* the documents shown */
	TS_UUID	sample_obj, note_obj;
	BOOL	tried[FACE_MAX];	/* faces opened to be looked at */
	char	fam[FAM_MAX][FNAME_MAX];
	UINT	fam_cls[FAM_MAX];
	INT	nfam;
} FSWIN;

LOCAL FSWIN	fsw;

/* The families there are: those of the box's faces, and the face's own */
LOCAL void fam_collect( void )
{
	INT	i, k;

	fsw.nfam = 0;
	for ( i = -1; i < nfrow && fsw.nfam < FAM_MAX; i++ ) {
		CONST char *nm = ( i < 0 ) ? fsw.f[fsw.cur].family : frow[i].s.family;
		UINT	cls = ( i < 0 ) ? fsw.f[fsw.cur].cls : frow[i].s.cls;

		if ( nm[0] == 0 ) continue;
		for ( k = 0; k < fsw.nfam && strcmp(fsw.fam[k], nm) != 0; k++ ) ;
		if ( k < fsw.nfam ) continue;
		snprintf(fsw.fam[fsw.nfam], FNAME_MAX, "%s", nm);
		fsw.fam_cls[fsw.nfam] = cls;
		fsw.nfam++;
	}
}

/* The list of families, the face's chosen */
LOCAL void fam_show( UI *u, INT num, CONST char *now )
{
	static char	line[FAM_MAX][FNAME_MAX + 16];
	CONST char	*lp[FAM_MAX];
	INT		k, cur = 0;

	for ( k = 0; k < fsw.nfam; k++ ) {
		snprintf(line[k], sizeof(line[k]), "%s\t%04X", fsw.fam[k], fsw.fam_cls[k] & 0xFFFFU);
		lp[k] = line[k];
		if ( strcmp(fsw.fam[k], now) == 0 ) cur = k + 1;
	}
	ui_names(u, num, lp, fsw.nfam);
	ui_set_value(u, num, cur);
}

LOCAL void fs_attr_show( void )
{
	FSET	*f = &fsw.f[fsw.cur];
	char	h[8];

	ui_set_value(&fsw.w, FS_BOLD, ( f->attr & FA_BOLD ) != 0);
	ui_set_value(&fsw.w, FS_ITALIC, ( f->attr & FA_ITALIC ) != 0);
	ui_set_value(&fsw.w, FS_BAG, ( f->attr & FA_BAG ) != 0);
	ui_set_value(&fsw.w, FS_SHADOW, ( f->attr & FA_SHADOW ) != 0);
	ui_set_value(&fsw.w, FS_PROP, ( f->attr & FA_PROP ) != 0);
	ui_set_value(&fsw.w, FS_VERT, ( f->attr & FA_VERT ) != 0);
	snprintf(h, sizeof(h), "%04X", f->attr & 0xFFFFU);
	ui_set_text(&fsw.w, FS_HEX, h);
}

/* The face chosen of the file, shown */
LOCAL void fs_show( void )
{
	FSET	*f = &fsw.f[fsw.cur];
	char	s[96];

	if ( fsw.size >= 1024 * 1024 ) {
		snprintf(s, sizeof(s), "%u KB（%u バイト）", (UINT)( fsw.size / 1024 ), (UINT)fsw.size);
	} else {
		snprintf(s, sizeof(s), "%u バイト", (UINT)fsw.size);
	}
	ui_set_text(&fsw.w, FS_SIZE, s);
	if ( fsw.count[fsw.cur] > 0 ) {
		snprintf(s, sizeof(s), "U+%04X〜U+%04X（%u 字）", (UINT)fsw.lo[fsw.cur], (UINT)fsw.hi[fsw.cur],
			 (UINT)fsw.count[fsw.cur]);
	} else {
		snprintf(s, sizeof(s), "（不明）");
	}
	ui_set_text(&fsw.w, FS_RANGE, s);
	ui_set_text(&fsw.w, FS_NAME, f->name);
	ui_set_value(&fsw.w, FS_USE, f->use ? 1 : 2);
	fs_attr_show();
	fam_collect();
	fam_show(&fsw.w, FS_FAMS, f->family);
	ui_off(&fsw.w, FS_UNIFY, fsw.nface <= 1);
}

/* What the parts hold, into the face chosen's settings */
LOCAL void fs_take( void )
{
	FSET	*f = &fsw.f[fsw.cur];
	char	t[FNAME_MAX];

	if ( ui_text(&fsw.w, FS_NAME, t, sizeof(t)) > 0 ) snprintf(f->name, sizeof(f->name), "%s", t);
	f->use = ( ui_value(&fsw.w, FS_USE) != 2 );
}

/* The attributes from the switches, or from the four digits when 詳細 is on */
LOCAL void fs_attr_take( INT id )
{
	FSET	*f = &fsw.f[fsw.cur];
	UINT	a = f->attr & ~FA_LOOK;

	if ( id == FS_HEX ) {
		char	h[16];
		UINT	v;

		(void)ui_text(&fsw.w, FS_HEX, h, sizeof(h));
		v = hex4(h);
		if ( v > 0xFFFFU ) return;	/* not four digits yet */
		f->attr = ( v & ~( FA_PROP | FA_VERT ) ) | ( f->attr & ( FA_PROP | FA_VERT ) );
		fs_attr_show();
		return;
	}
	if ( ui_value(&fsw.w, FS_BOLD) != 0 ) a |= FA_BOLD;
	if ( ui_value(&fsw.w, FS_ITALIC) != 0 ) a |= FA_ITALIC;
	if ( ui_value(&fsw.w, FS_BAG) != 0 ) a |= FA_BAG;
	if ( ui_value(&fsw.w, FS_SHADOW) != 0 ) a |= FA_SHADOW;
	/* 袋文字 and 影付文字 are not both */
	if ( ( a & FA_BAG ) != 0 && ( a & FA_SHADOW ) != 0 ) {
		a &= ( id == FS_BAG ) ? ~FA_SHADOW : ~FA_BAG;
	}
	f->attr = a;
	fs_attr_show();
}

/* ---- the documents of 見本 and 注釈 */

/* A document of the text editor holding xml, made for the window and not to be edited */
LOCAL ER doc_make( CONST char *name, CONST char *xml, TS_UUID *p_u )
{
	T_OBCRE	c;
	char	json[400];
	INT	recno = -1;
	SZ	asz = 0;
	ID	k;
	ER	er;

	snprintf(json, sizeof(json),
		 "{\"name\":\"%s\",\"relationship\":[],\"linktype\":false,\"refCount\":0,"
		 "\"editable\":false,\"deletable\":true,\"readable\":true,"
		 "\"applist\":{\"basic-text-editor\":{\"name\":\"基本文章編集\",\"defaultOpen\":true}}}", name);
	memset(&c, 0, sizeof(c));
	c.type = OB_T_STORAGE;
	c.sub = OB_S_FILE;
	c.json = (CONST UB *)json;
	c.jsonsz = (SZ)strlen(json);
	er = ob_cre_obj(&c, p_u);
	if ( er < E_OK ) return er;
	k = ob_opn_obj(p_u, OB_OP_R | OB_OP_W);
	if ( k <= 0 ) {
		(void)ob_del_obj(p_u);
		return (ER)k;
	}
	er = ob_apd_rec(k, OB_RT_TAD, 0, &recno);
	if ( er >= E_OK ) er = ob_wri_rec(k, recno, 0, xml, (SZ)strlen(xml), &asz);
	ob_cls_obj(k);
	if ( er < E_OK ) (void)ob_del_obj(p_u);
	return er;
}

/* Text put in a document: what XML takes as it is */
LOCAL void xml_put( char *b, INT max, CONST char *s )
{
	INT	k = (INT)strlen(b);

	for ( ; *s != 0 && k < max - 6; s++ ) {
		CONST char *e = ( *s == '<' ) ? "&lt;" : ( *s == '>' ) ? "&gt;" : ( *s == '&' ) ? "&amp;"
				: ( *s == '"' ) ? "&quot;" : NULL;

		if ( e != NULL ) {
			while ( *e != 0 ) b[k++] = *e++;
		} else {
			b[k++] = *s;
		}
	}
	b[k] = 0;
}

LOCAL void doc_close( BOOL *on, TS_UUID *u )
{
	T_DTANS	an;

	if ( !*on ) return;
	(void)desk_req(DT_RQ_CLOSE, u, -1, 0, NULL, &an);
	(void)ob_del_obj(u);
	*on = FALSE;
}

LOCAL CONST char *CONST sample_text[] = {
	"あいうえお　アイウエオ　漢字かな交じり文",
	"ABCDEFGHIJKLM abcdefghijklm 0123456789",
	"「、。」！？（）・ー〜　永遠の東京",
};
LOCAL CONST INT	sample_size[] = { 12, 18, 24, 36 };

/* 見本: the face's letters at four sizes, in a window of the text editor */
LOCAL void sample_show( BOOL on )
{
	static char	xml[4096];
	T_DTANS		an;
	char		face[DT_TEXT_MAX], name[96], s[96];
	INT		i, j;
	ER		er;

	if ( !on ) {
		doc_close(&fsw.sample, &fsw.sample_obj);
		tm_putstring((CONST UB *)"sysenv: sample off\n");
		return;
	}
	if ( fsw.sample ) return;
	er = desk_req(DT_RQ_FONT_TRY, &fsw.obj, fsw.cur, 0, NULL, &an);
	if ( er < E_OK ) {
		tell("書体の見本を表示できません。");
		ui_set_value(&fsw.w, FS_SAMPLE, 0);
		return;
	}
	fsw.tried[fsw.cur] = TRUE;
	snprintf(face, sizeof(face), "%s", (CONST char *)an.text);
	snprintf(name, sizeof(name), "見本：%s", fsw.f[fsw.cur].name);
	snprintf(xml, sizeof(xml), "<tad version=\"1.0\" encoding=\"UTF-8\" filename=\"");
	xml_put(xml, sizeof(xml), name);
	strncat(xml, "\">\n<document>\n", sizeof(xml) - strlen(xml) - 1);
	for ( j = 0; j < 4; j++ ) {
		for ( i = 0; i < 3; i++ ) {
			snprintf(s, sizeof(s), "<p><font size=\"%d\"/><font face=\"", sample_size[j]);
			strncat(xml, s, sizeof(xml) - strlen(xml) - 1);
			xml_put(xml, sizeof(xml), face);
			strncat(xml, "\"/>", sizeof(xml) - strlen(xml) - 1);
			xml_put(xml, sizeof(xml), sample_text[i]);
			strncat(xml, "</p>\n", sizeof(xml) - strlen(xml) - 1);
		}
	}
	strncat(xml, "</document>\n</tad>\n", sizeof(xml) - strlen(xml) - 1);
	if ( doc_make(name, xml, &fsw.sample_obj) < E_OK
	  || desk_req(DT_RQ_OPEN, &fsw.sample_obj, -1, 0, NULL, &an) < E_OK ) {
		tell("書体の見本を表示できません。");
		ui_set_value(&fsw.w, FS_SAMPLE, 0);
		return;
	}
	fsw.sample = TRUE;
	snprintf(s, sizeof(s), "sysenv: sample on %s\n", face);
	tm_putstring((CONST UB *)s);
}

/* 注釈: what the face's file says of itself, in a window of the text editor */
LOCAL void note_show( BOOL on )
{
	static CONST struct {
		UW		id;
		CONST char	*label;
	} what[] = {
		{ 4, "名前" }, { 1, "ファミリ" }, { 2, "スタイル" }, { 5, "バージョン" },
		{ 0, "著作権" }, { 7, "商標" }, { 8, "製作者" }, { 9, "デザイナー" },
		{ 10, "説明" }, { 11, "製作者のURL" }, { 13, "ライセンス" }, { 14, "ライセンスのURL" },
	};
	static char	xml[16384], v[2048];
	T_DTANS		an;
	char		name[96];
	SFNT		sf;
	UB		*t = NULL;
	UW		len = 0;
	INT		i, lines = 0;

	if ( !on ) {
		doc_close(&fsw.note, &fsw.note_obj);
		tm_putstring((CONST UB *)"sysenv: note off\n");
		return;
	}
	if ( fsw.note ) return;
	if ( sf_open(&fsw.obj, fsw.cur, &sf) >= E_OK ) {
		t = sf_table(&sf, TAG('n', 'a', 'm', 'e'), 256 * 1024, &len);
		sf_close(&sf);
	}
	snprintf(name, sizeof(name), "注釈：%s", fsw.f[fsw.cur].name);
	snprintf(xml, sizeof(xml), "<tad version=\"1.0\" encoding=\"UTF-8\" filename=\"");
	xml_put(xml, sizeof(xml), name);
	strncat(xml, "\">\n<document>\n", sizeof(xml) - strlen(xml) - 1);
	for ( i = 0; t != NULL && i < (INT)( sizeof(what) / sizeof(what[0]) ); i++ ) {
		sf_name(t, len, what[i].id, TRUE, v, sizeof(v));
		if ( v[0] == 0 ) continue;
		strncat(xml, "<p>", sizeof(xml) - strlen(xml) - 1);
		xml_put(xml, sizeof(xml), what[i].label);
		strncat(xml, "：", sizeof(xml) - strlen(xml) - 1);
		xml_put(xml, sizeof(xml), v);
		strncat(xml, "</p>\n", sizeof(xml) - strlen(xml) - 1);
		lines++;
	}
	if ( t != NULL ) free(t);
	if ( lines == 0 ) strncat(xml, "<p>注釈はありません。</p>\n", sizeof(xml) - strlen(xml) - 1);
	strncat(xml, "</document>\n</tad>\n", sizeof(xml) - strlen(xml) - 1);
	if ( doc_make(name, xml, &fsw.note_obj) < E_OK
	  || desk_req(DT_RQ_OPEN, &fsw.note_obj, -1, 0, NULL, &an) < E_OK ) {
		tell("書体の注釈を表示できません。");
		ui_set_value(&fsw.w, FS_NOTE, 0);
		return;
	}
	fsw.note = TRUE;
	{
		char	s[64];

		snprintf(s, sizeof(s), "sysenv: note on %d\n", lines);
		tm_putstring((CONST UB *)s);
	}
}

/* ---- opened and closed */

/*
 * The window for a face object: reg for a file dropped, else for the
 * face 'cur' of an object of the box. FALSE when the object is not a
 * font file.
 */
LOCAL BOOL fset_open( CONST TS_UUID *u, INT cur, BOOL reg )
{
	FSET		said[FACE_MAX];
	INT		idx[FACE_MAX], ns, k, i;
	SFNT		sf;
	SFINFO		si;
	CONST char	*lp[FACE_MAX];
	static char	line[FACE_MAX][FNAME_MAX + 8];
	char		s[160];

	if ( sf_open(u, 0, &sf) < E_OK ) return FALSE;
	memset(&fsw, 0, sizeof(fsw));
	fsw.obj = *u;
	fsw.reg = reg;
	fsw.size = sf.size;
	fsw.nface = ( sf.nface > FACE_MAX ) ? FACE_MAX : sf.nface;
	sf_close(&sf);
	ns = faces_said(u, said, idx, FACE_MAX);
	for ( k = 0; k < fsw.nface; k++ ) {
		FSET	*sd = NULL;

		memset(&si, 0, sizeof(si));
		if ( sf_open(u, k, &sf) >= E_OK ) {
			sf_info(&sf, &si);
			sf_close(&sf);
		}
		for ( i = 0; i < ns; i++ ) {
			if ( idx[i] == k ) sd = &said[i];
		}
		fset_of(&si, sd, &fsw.f[k]);
		fsw.was[k] = fsw.f[k];
		snprintf(fsw.full[k], FNAME_MAX, "%s", si.full);
		fsw.lo[k] = si.lo;
		fsw.hi[k] = si.hi;
		fsw.count[k] = si.count;
		snprintf(line[k], sizeof(line[k]), "%d：%s", k + 1, si.full);
		lp[k] = line[k];
	}
	fsw.cur = ( cur >= 0 && cur < fsw.nface ) ? cur : 0;
	ui_icon(SYSDEF_PROG_SYSENV);
	if ( ui_open(&fsw.w, SYSDEF_BOX_SYSENV, PN_FONTSET, 200, 110) < E_OK ) {
		tell("フォント設定のウインドウを開けません。");
		return TRUE;
	}
	fsw.open = TRUE;
	ask_win = &fsw.w.win;
	ui_names(&fsw.w, FS_FACES, lp, fsw.nface);
	ui_set_value(&fsw.w, FS_FACES, fsw.cur + 1);
	ui_off(&fsw.w, FS_HEX, TRUE);
	fs_show();
	snprintf(s, sizeof(s), "sysenv: fontset open %s faces %d %s range %u\n", fsw.f[fsw.cur].name,
		 fsw.nface, reg ? "register" : "registered", (UINT)fsw.count[fsw.cur]);
	tm_putstring((CONST UB *)s);
	return TRUE;
}

LOCAL void fset_close( void )
{
	T_DTANS	an;
	INT	k;

	if ( !fsw.open ) return;
	doc_close(&fsw.sample, &fsw.sample_obj);
	doc_close(&fsw.note, &fsw.note_obj);
	for ( k = 0; k < fsw.nface; k++ ) {
		if ( fsw.tried[k] ) (void)desk_req(DT_RQ_FONT_TRY, &fsw.obj, k, DT_F_FONT_DONE, NULL, &an);
	}
	ui_close(&fsw.w);
	fsw.open = FALSE;
	ask_win = NULL;
	tm_putstring((CONST UB *)"sysenv: fontset closed\n");
}

/* Whether a face of the box is of the same family and look as f (the 更新 a registration may be) */
LOCAL BOOL same_there( CONST FSET *f )
{
	INT	i;

	for ( i = 0; i < nfrow; i++ ) {
		if ( strcmp(frow[i].s.family, f->family) == 0
		  && ( frow[i].s.attr & FA_LOOK ) == ( f->attr & FA_LOOK ) ) return TRUE;
	}
	return FALSE;
}

/* The face object registered, its faces set as the window has them; update: the same replaced */
LOCAL ER fs_register( BOOL update )
{
	T_DTANS	an;
	TS_UUID	nu;
	char	j[DT_CARRY_TEXT];
	INT	k;
	ER	er;

	fset_json(&fsw.f[0], j, sizeof(j));
	er = desk_req(DT_RQ_FONT_REG, &fsw.obj, 0, update ? DT_F_FONT_UPDATE : 0, j, &an);
	nu = an.into;
	for ( k = 1; er >= E_OK && k < fsw.nface; k++ ) {
		fset_json(&fsw.f[k], j, sizeof(j));
		er = desk_req(DT_RQ_FONT_SET, &nu, k, 0, j, &an);
	}
	return er;
}

/* The faces whose settings changed, set */
LOCAL ER fs_update( void )
{
	T_DTANS	an;
	char	j[DT_CARRY_TEXT];
	INT	k;
	ER	er = E_OK;

	for ( k = 0; er >= E_OK && k < fsw.nface; k++ ) {
		if ( memcmp(&fsw.f[k], &fsw.was[k], sizeof(FSET)) == 0 ) continue;
		fset_json(&fsw.f[k], j, sizeof(j));
		er = desk_req(DT_RQ_FONT_SET, &fsw.obj, k, 0, j, &an);
	}
	return er;
}

/*
 * 閉じる: what is to become of the settings asked. A file dropped is
 * registered (登録して終了), registered in place of the face of the same
 * family and look (更新して終了), or not; a face of the box changed is
 * set as it now is (更新して終了), or not. 取消 leaves the window open.
 */
LOCAL void fs_end( void )
{
	CONST char *l[2];
	char	l1[128], l2[128], s[160];
	BOOL	changed = FALSE;
	INT	k, a;
	ER	er = E_OK;
	CONST char *how = "none";

	fs_take();
	if ( ui_value(&fsw.w, FS_UNIFY) != 0 ) {
		for ( k = 0; k < fsw.nface; k++ ) fsw.f[k].cls = fsw.f[fsw.cur].cls;
	}
	for ( k = 0; k < fsw.nface; k++ ) {
		if ( memcmp(&fsw.f[k], &fsw.was[k], sizeof(FSET)) != 0 ) changed = TRUE;
	}
	snprintf(l1, sizeof(l1), "〈%s〉", fsw.f[fsw.cur].name);
	l[0] = l1;
	l[1] = l2;
	if ( fsw.reg ) {
		BOOL	same = same_there(&fsw.f[0]);

		snprintf(l2, sizeof(l2), same ? "同じファミリと属性の書体が登録されています。登録しますか？"
					     : "この書体を登録しますか？");
		a = ui_ask(&fsw.w, same ? PN_FONT_END_SAME : PN_FONT_END_NEW, l, 2);
		if ( a == B_FONT_REG_END ) {
			er = fs_register(FALSE);
			how = "register";
		} else if ( a == B_FONT_UPD_END ) {
			er = fs_register(TRUE);
			how = "update";
		} else if ( a != B_FONT_NOT_END ) {
			return;
		}
	} else if ( changed ) {
		snprintf(l2, sizeof(l2), "設定を変更しました。更新しますか？");
		a = ui_ask(&fsw.w, PN_FONT_END_UPD, l, 2);
		if ( a == B_FONT_UPD_END ) {
			er = fs_update();
			how = "update";
		} else if ( a != B_FONT_NOT_END ) {
			return;
		}
	}
	snprintf(s, sizeof(s), "sysenv: fontset end %s %s (%d)\n", how, fsw.f[fsw.cur].family, (INT)er);
	if ( er != E_ABORT ) {
		fset_close();
		fill_fonts();
	}
	tm_putstring((CONST UB *)s);		/* told once the list shows it */
	if ( er == E_OACV ) {
		tell("書体を登録する権限がありません。");
	} else if ( er < E_OK && er != E_ABORT ) {
		tell("書体を登録できませんでした。");
	}
	/* the administrator's password not given: the window stays open */
}

/* ---- ファミリ: the family and the base, in a panel of their own */

LOCAL void fd_class_show( UI *q, INT num, UINT cls )
{
	char	h[8];

	snprintf(h, sizeof(h), "%04X", cls & 0xFFFFU);
	ui_set_text(q, num, h);
}

/* Whether a family is one a face of another object of the box has: it has its base already */
LOCAL BOOL fam_taken( CONST char *nm )
{
	INT	k;

	for ( k = 0; k < nfrow; k++ ) {
		if ( strcmp(frow[k].s.family, nm) == 0 && !same_obj(&frow[k].obj, &fsw.obj) ) return TRUE;
	}
	return FALSE;
}

/* The base's parts, off for a family there is already */
LOCAL void fd_base_off( UI *q )
{
	char	t[FNAME_MAX];
	BOOL	have;

	(void)ui_text(q, FD_NAME, t, sizeof(t));
	have = fam_taken(t);
	ui_off(q, FD_BNAME, have);
	ui_off(q, FD_BCLASS, have);
	ui_off(q, FD_BLIST, have);
}

LOCAL void fs_family( void )
{
	UI	q;
	T_OBNTM	m;
	INT	ev, num, k;
	BOOL	done = FALSE, set = FALSE;
	FSET	*f = &fsw.f[fsw.cur];
	char	t[FNAME_MAX];

	if ( ui_dlg_open(&fsw.w, PN_FONT_FAMILY, &q) < E_OK ) return;
	ui_set_text(&q, FD_NAME, f->family);
	fd_class_show(&q, FD_CLASS, f->cls);
	fam_show(&q, FD_LIST, f->family);
	ui_set_text(&q, FD_BNAME, f->base);
	fd_class_show(&q, FD_BCLASS, 0);
	for ( k = 0; k < fsw.nfam; k++ ) {
		if ( strcmp(fsw.fam[k], f->base) == 0 ) fd_class_show(&q, FD_BCLASS, fsw.fam_cls[k]);
	}
	fam_show(&q, FD_BLIST, f->base);
	fd_base_off(&q);
	tm_putstring((CONST UB *)"sysenv: family panel\n");
	while ( !done ) {
		ev = ui_dlg_next(&fsw.w, &q, &m, &num);
		if ( ev == UI_EV_NONE ) {
			(void)tk_dly_tsk(TICK_MS);
			continue;
		}
		if ( ev == UI_EV_CLOSE ) {
			done = TRUE;
		} else if ( ev == UI_EV_BUTTON ) {
			done = TRUE;
			set = ( num == FD_SET );
		} else if ( ev == UI_EV_CHANGE && ( num == FD_LIST || num == FD_BLIST ) ) {
			k = ui_value(&q, num) - 1;
			if ( k >= 0 && k < fsw.nfam ) {
				ui_set_text(&q, ( num == FD_LIST ) ? FD_NAME : FD_BNAME, fsw.fam[k]);
				fd_class_show(&q, ( num == FD_LIST ) ? FD_CLASS : FD_BCLASS, fsw.fam_cls[k]);
			}
		}
		if ( !done && ev == UI_EV_CHANGE ) {
			fd_base_off(&q);	/* a family there is already has its base: none is given here */
		}
	}
	if ( set ) {
		UINT	c;

		(void)ui_text(&q, FD_NAME, t, sizeof(t));
		if ( t[0] != 0 ) snprintf(f->family, sizeof(f->family), "%s", t);
		(void)ui_text(&q, FD_CLASS, t, sizeof(t));
		c = hex4(t);
		if ( c <= 0xFFFFU ) f->cls = c;
		if ( fam_taken(f->family) ) {
			f->base[0] = 0;
		} else {
			(void)ui_text(&q, FD_BNAME, t, sizeof(t));
			snprintf(f->base, sizeof(f->base), "%s", ( strcmp(t, f->family) == 0 ) ? "" : t);
		}
		{
			char	s[FNAME_MAX * 2 + 48];

			snprintf(s, sizeof(s), "sysenv: family %s %04X base %s\n", f->family, f->cls & 0xFFFFU,
				 ( f->base[0] != 0 ) ? f->base : "-");
			tm_putstring((CONST UB *)s);
		}
	}
	ui_dlg_close(&q);
	fs_show();
}

/* ---- what is done in the window */

LOCAL void fs_menu( CONST T_OBNTM *m )
{
	TS_UUID	def, vw;
	T_MNSEL	sel;
	ID	mid;
	BOOL	chosen;

	if ( ui_uuid(SYSDEF_MENU_FONTSET, &def) < E_OK || mn_cre_men(&def, &mid) < E_OK ) return;
	if ( ui_uuid(SYSDEF_MENU_FONTSET_VIEW, &vw) >= E_OK ) {
		(void)mn_chg_atr(mid, &vw, "sample", fsw.sample ? MN_TICK : 0);
		(void)mn_chg_atr(mid, &vw, "note", fsw.note ? MN_TICK : 0);
	}
	chosen = (BOOL)( mn_pop_men(mid, fsw.w.kw, m->x, m->y, m->when, &sel) >= E_OK );
	(void)mn_del_men(mid);
	if ( !chosen ) return;
	if ( is_code(&sel, "close") ) {
		fs_end();
	} else if ( is_code(&sel, "sample") ) {
		sample_show(!fsw.sample);
		ui_set_value(&fsw.w, FS_SAMPLE, fsw.sample);
	} else if ( is_code(&sel, "note") ) {
		note_show(!fsw.note);
		ui_set_value(&fsw.w, FS_NOTE, fsw.note);
	}
}

LOCAL void fs_event( CONST T_OBNTM *m )
{
	INT	id, k;

	switch ( ui_event(&fsw.w, m, &id) ) {
	case UI_EV_CLOSE:
		fs_end();
		break;
	case UI_EV_MENU:
		fs_menu(m);
		break;
	case UI_EV_KEY:
		if ( ( m->mods & ( 0x01 | 0x10 ) ) != 0 ) {
			UB	c = wm_key_char(m->code, m->mods);

			if ( c == 'e' || c == 'E' ) fs_end();
		}
		break;
	case UI_EV_BUTTON:
		if ( id == FS_FAMDET ) fs_family();
		break;
	case UI_EV_CHANGE:
		switch ( id ) {
		case FS_FACES:
			k = ui_value(&fsw.w, FS_FACES) - 1;
			if ( k >= 0 && k < fsw.nface && k != fsw.cur ) {
				char	s[FNAME_MAX + 40];

				fs_take();
				fsw.cur = k;
				fs_show();
				snprintf(s, sizeof(s), "sysenv: fontset face %d %s\n", k + 1, fsw.f[k].name);
				tm_putstring((CONST UB *)s);
			}
			break;
		case FS_SAMPLE:
			sample_show(ui_value(&fsw.w, FS_SAMPLE) != 0);
			break;
		case FS_NOTE:
			note_show(ui_value(&fsw.w, FS_NOTE) != 0);
			break;
		case FS_NAME:
		case FS_USE:
			fs_take();
			break;
		case FS_BOLD:
		case FS_ITALIC:
		case FS_BAG:
		case FS_SHADOW:
		case FS_HEX:
			fs_attr_take(id);
			break;
		case FS_DETAIL:
			ui_off(&fsw.w, FS_HEX, ui_value(&fsw.w, FS_DETAIL) == 0);
			break;
		case FS_FAMS:
			k = ui_value(&fsw.w, FS_FAMS) - 1;
			if ( k >= 0 && k < fsw.nfam ) {
				char	s[FNAME_MAX + 40];

				memcpy(fsw.f[fsw.cur].family, fsw.fam[k], FNAME_MAX);
				fsw.f[fsw.cur].cls = fsw.fam_cls[k];
				snprintf(s, sizeof(s), "sysenv: fontset family %s %04X\n", fsw.f[fsw.cur].family,
					 fsw.f[fsw.cur].cls & 0xFFFFU);
				tm_putstring((CONST UB *)s);
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

/* 詳細情報: the face chosen, in フォント設定 */
LOCAL void font_detail( void )
{
	INT	k = font_pick("書体を選んでください。");

	if ( k < 0 ) return;
	if ( fsw.open ) {
		tell("フォント設定のウインドウを閉じてから操作してください。");
		return;
	}
	(void)fset_open(&frow[k].obj, frow[k].index, FALSE);
}

/* Whether an object is one the box links to */
LOCAL BOOL in_box( CONST TS_UUID *u )
{
	INT	i;

	for ( i = 0; i < nfrow; i++ ) {
		if ( same_obj(&frow[i].obj, u) ) return TRUE;
	}
	return FALSE;
}

/*
 * A link dropped on 〈書体〉: a face saved (an object with its settings)
 * is registered, or registered in place of the same, as the panel asks;
 * a font file is opened in フォント設定 to be set and registered.
 */
LOCAL void font_drop( CONST T_OBDROP *d )
{
	CONST char *l[2];
	char	l1[128], l2[128], s[128];
	SFNT	sf;
	SFINFO	si;
	T_DTANS	an;
	INT	i, a;
	ER	er;

	for ( i = 0; i < d->n && i < OB_DROP_MAX; i++ ) {
		CONST TS_UUID *u = &d->v[i].target;
		FSET	f;

		if ( sf_open(u, 0, &sf) < E_OK ) {
			tell("書体の実身ではありません。");
			continue;
		}
		sf_info(&sf, &si);
		sf_close(&sf);
		if ( in_box(u) ) {
			tell("この書体はすでに登録されています。");
			continue;
		}
		if ( !has_settings(u) ) {
			tm_putstring((CONST UB *)"sysenv: font drop file\n");
			if ( fsw.open ) {
				tell("フォント設定のウインドウを閉じてから操作してください。");
				return;
			}
			(void)fset_open(u, 0, TRUE);
			return;
		}
		{
			FSET	said[1];
			INT	idx[1];

			(void)faces_said(u, said, idx, 1);
			fset_of(&si, said, &f);
		}
		tm_putstring((CONST UB *)"sysenv: font drop saved\n");
		snprintf(l1, sizeof(l1), "〈%s〉を登録します。", f.name);
		snprintf(l2, sizeof(l2), same_there(&f) ? "同じファミリと属性の書体があるときは、更新できます。"
						      : "登録すると、すぐに使えます。");
		l[0] = l1;
		l[1] = l2;
		a = ui_ask(&ui, same_there(&f) ? PN_FONT_REG_SAME : PN_FONT_REG, l, 2);
		if ( a != B_FONT_REG && a != B_FONT_UPD ) continue;
		er = desk_req(DT_RQ_FONT_REG, u, 0, ( a == B_FONT_UPD ) ? DT_F_FONT_UPDATE : 0, NULL, &an);
		snprintf(s, sizeof(s), "sysenv: font register %s %s (%d)\n", f.name,
			 ( a == B_FONT_UPD ) ? "update" : "new", (INT)er);
		fill_fonts();
		tm_putstring((CONST UB *)s);	/* told once the list shows it */
		if ( er == E_OACV ) {
			tell("書体を登録する権限がありません。");
		} else if ( er < E_OK && er != E_ABORT ) {
			tell("書体を登録できませんでした。");
		}
	}
}

/* Links dropped on the window: packages on 〈バージョン〉, faces on 〈書体〉 */
LOCAL void drop( void )
{
	static T_OBDROP	d;

	if ( ui_drop(&ui, &d) < E_OK ) return;
	if ( ui_sheet(&ui) == S_VERSION ) {
		ui_drop_answer(&ui, &d, OB_DR_ACCEPT, "");
		prog_drop(&d);
	} else if ( ui_sheet(&ui) == S_FONT ) {
		ui_drop_answer(&ui, &d, OB_DR_ACCEPT, "");
		font_drop(&d);
	} else {
		ui_drop_answer(&ui, &d, OB_DR_REFUSE, "〈書体〉か〈バージョン〉の一覧に落としてください");
	}
}

/* ---------------------------------------------------------------- the menu */

LOCAL BOOL is_code( CONST T_MNSEL *sel, CONST char *code )
{
	return (BOOL)( strcmp((CONST char *)sel->code, code) == 0 );
}

LOCAL void menu( CONST T_OBNTM *m )
{
	TS_UUID	def;
	T_MNSEL	sel;
	ID	mid;
	BOOL	chosen;

	if ( ui_uuid(SYSDEF_MENU_SYSENV, &def) < E_OK || mn_cre_men(&def, &mid) < E_OK ) return;
	{
		TS_UUID	od;
		INT	k = ui_value(&ui, P_PROGS) - 1;

		INT	f = ui_value(&ui, P_FONTS) - 1;
		BOOL	font = (BOOL)( ui_sheet(&ui) == S_FONT && f >= 0 && f < nfrow );

		/*
		 * 保存: a face chosen on 〈書体〉; 削除: that, or a program chosen
		 * on 〈バージョン〉 that is not the system's own
		 */
		if ( ui_uuid(SYSDEF_MENU_SYSENV_OP, &od) >= E_OK ) {
			(void)mn_chg_atr(mid, &od, "font.save", font ? 0 : MN_GREY);
			(void)mn_chg_atr(mid, &od, "delete",
					 ( font || ( ui_sheet(&ui) == S_VERSION && k >= 0 && k < nprog
						     && !prog_info[k].builtin ) ) ? 0 : MN_GREY);
		}
	}
	chosen = (BOOL)( mn_pop_men(mid, ui.kw, m->x, m->y, m->when, &sel) >= E_OK );
	(void)mn_del_men(mid);
	if ( !chosen ) return;
	if ( is_code(&sel, "close") ) {
		closing = TRUE;
	} else if ( is_code(&sel, "delete") ) {
		if ( ui_sheet(&ui) == S_FONT ) {
			font_delete();
		} else {
			prog_delete();
		}
	} else if ( is_code(&sel, "font.save") ) {
		font_save();
	} else if ( is_code(&sel, "again") ) {
		fill_power();
		fill_walls();
		fill_schemes();
		look_fill();
		fill_fonts();
		fill_version();
	}
}

LOCAL void changed( INT id )
{
	if ( look_changed(id) ) {
		return;
	}
	switch ( id ) {
	case P_WALLS:
	case P_WALL_MODE:
		wall_set();
		break;
	case P_SCHEMES:
		scheme_set();
		break;
	case P_FONTS:
		font_chosen();
		break;
	case P_FONT_USE:
		font_use();
		break;
	case P_WD_ON:
		ui_off(&ui, P_WD_SEC, ui_value(&ui, P_WD_ON) == 0);
		break;
	case P_SND_OUTS:
		sound_set();
		break;
	case P_SND_VOL:
		sound_level();
		break;
	case P_SND_KEEP:
		sound_keep();
		break;
	default:
		break;
	}
}

LOCAL void pressed( INT id )
{
	switch ( id ) {
	case P_SHOW:
	{
		INT	k = ui_value(&ui, P_DEVS) - 1;

		if ( k >= 0 && k < NDEV ) open_page(dev_pages[k]);
		break;
	}
	case P_BACK:
	case P_SCR_CANCEL:
	case P_KBD_CANCEL:
		ui_show_sheet(&ui, S_DEVS);
		break;
	case P_SCR_SET:
		screen_set();
		break;
	case P_KBD_SET:
		kbd_set();
		break;
	case P_PERF_SET:
		perf_set();
		break;
	case P_USB_AGAIN:
		fill_usb();
		break;
	case P_PROG_INFO:
		prog_detail();
		break;
	case P_FONT_INFO:
		font_detail();
		break;
	case P_SND_TEST:
		sound_test();
		break;
	case P_RESTART:
		power(TRUE);
		break;
	case P_OFF:
		power(FALSE);
		break;
	case P_LK_DEFAULT:
		look_all(TRUE);
		break;
	case P_LK_CANCEL:
		look_all(FALSE);
		break;
	default:
		break;
	}
}

int main( void )
{
	T_OBNTM	m;
	SZ	asz = 0;
	INT	id, ev;

	ui_icon(SYSDEF_PROG_SYSENV);
	if ( ui_open(&ui, SYSDEF_BOX_SYSENV, PN_MAIN, 160, 80) < E_OK ) {
		tm_putstring((CONST UB *)"sysenv: no window\n");
		return 1;
	}
	ui_names(&ui, P_DEVS, dev_names, NDEV);
	ui_set_value(&ui, P_DEVS, 1);
	fill_power();
	fill_walls();
	fill_schemes();
	look_start();
	fill_fonts();
	fill_version();
	tm_putstring((CONST UB *)"sysenv: ready\n");

	while ( !closing ) {
		while ( !closing && ob_rea_rec(ui.port, 0, 0, &m, sizeof(m), &asz) >= E_OK ) {
			ev = ui_event(&ui, &m, &id);
			switch ( ev ) {
			case UI_EV_CLOSE:
				closing = TRUE;
				break;
			case UI_EV_MENU:
				menu(&m);
				break;
			case UI_EV_DROP:
				drop();
				break;
			case UI_EV_KEY:
				if ( ( m.mods & ( 0x01 | 0x10 ) ) != 0 ) {
					UB	c = wm_key_char(m.code, m.mods);

					if ( c == 'e' || c == 'E' ) closing = TRUE;
				}
				break;
			case UI_EV_CHANGE:
				changed(id);
				break;
			case UI_EV_BUTTON:
				pressed(id);
				break;
			default:
				break;
			}
		}
		/* フォント設定, while it is open */
		while ( !closing && fsw.open && ob_rea_rec(fsw.w.port, 0, 0, &m, sizeof(m), &asz) >= E_OK ) {
			fs_event(&m);
		}
		if ( !closing ) {
			(void)tk_dly_tsk(TICK_MS);
			if ( lk_unkept && ++lk_quiet >= LK_KEEP_TICKS ) {
				look_keep();		/* the look, once the hand has stopped */
			}
			if ( ui_sheet(&ui) == S_POWER && ++perf_tick >= PERF_TICKS ) {
				fill_perf_state();	/* the clock and the temperature as they go */
			}
			if ( ui_sheet(&ui) == S_D_SOUND ) {
				sound_tick();		/* outputs plugged in and out */
			}
		}
	}
	look_keep();
	fset_close();
	if ( snd_key > 0 ) ob_cls_obj(snd_key);
	ui_close(&ui);
	tm_putstring((CONST UB *)"sysenv: closed\n");
	return 0;
}
