/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	netenv.c
 *	ネットワーク設定: the accessory (design 16.5.23)
 *
 *	The sheet 〈ネットワーク〉〈アドレス〉 holds the machine's name and
 *	address, the two name servers with their names, the domain, the
 *	gateway and the subnet mask; 〈ネットワーク〉〈状態〉 shows what the
 *	interface has now. An address of 0.0.0.0 asks the network for one
 *	(DHCP), and name servers of 0.0.0.0 take the network's.
 *
 *	〈ＩＰｖ６〉〈設定〉 says whether IPv6 is used, and how the interface
 *	has its addresses: from the routers' advertisements (with temporary
 *	addresses as well, when asked), a fixed one with its prefix length
 *	and router, or none but the link-local one; and two IPv6 name
 *	servers, asked before those the routers name. 〈ＩＰｖ６〉〈状態〉
 *	shows the link-local address, the others with their lifetimes, the
 *	router and the name servers.
 *
 *	What is set is written to the network's settings object
 *	(SYSDEF_CONF_NET) as its lines (include/ts/conf.h) when the window
 *	closes with 保存, or at once with 設定を更新 [S]; the system takes
 *	them as it sees the object written, and again at every start. When
 *	other programs have sockets open -- the system object says how many
 *	(OB_SYS_NET) -- the person is told the new settings reach them only
 *	once they have finished, and may give up. What the interface has
 *	now is read from the same record.
 */

#include <string.h>
#include <stdio.h>
#include <ts/ui.h>
#include <ts/conf.h>
#include <ts/sysdef.h>
#include <ts/uuid.h>

#define TICK_MS		20

/* The parts, by the numbers the data box gives them */
#define P_HOST		11
#define P_HOST_IP	12
#define P_DNS1		13
#define P_DNS1_IP	14
#define P_DNS2		15
#define P_DNS2_IP	16
#define P_DOM		17
#define P_GW		18
#define P_GW_IP		19
#define P_MASK		20
#define P_ST_ADDR	31
#define P_ST_MASK	32
#define P_ST_GW		33
#define P_ST_DNS	34
#define P_ST_AGAIN	35
#define P6_ON		41
#define P6_HOW		42		/* 1 自動, 2 固定, 3 使わない */
#define P6_TEMP		43
#define P6_ADDR		44
#define P6_PLEN		45
#define P6_GW		46
#define P6_DNS1		47
#define P6_DNS2		48
#define P6_ST_LINK	51
#define P6_ST_LIST	52
#define P6_ST_GW	53
#define P6_ST_DNS	54
#define P6_ST_AGAIN	55
#define P6_ST_MODE	56

/* Its panels: the window's, and those it asks with */
#define PN_MAIN		1
#define PN_WARN		10
#define PN_UPDATE	11
#define PN_FINISH	12
#define PN_BUSY		13
#define B_UPDATE	103
#define B_NOSAVE	104
#define B_SAVE		105

#define TAG_STATE	2		/* 〈ネットワーク〉〈状態〉 */
#define TAG_STATE6	4		/* 〈ＩＰｖ６〉〈状態〉 */

LOCAL UI	ui;
LOCAL T_CFNET	was;			/* as the settings stood when read or written */
LOCAL T_CFNET6	was6;
LOCAL UB	text[CF_TEXT_MAX];	/* the record */
LOCAL INT	tlen;
LOCAL BOOL	closing;

/* The system object's record of the network, as its lines; the length, or an error */
LOCAL UB	net_rec[4096];

LOCAL INT net_read( void )
{
	TS_UUID	u;
	ID	key;
	SZ	asz = 0;
	ER	er;

	if ( ui_uuid(SYSDEF_SYSTEM, &u) < E_OK ) return E_SYS;
	key = ob_opn_obj(&u, OB_OP_R);
	if ( key < E_OK ) return (INT)key;
	er = ob_rea_rec(key, OB_SYS_NET, 0, net_rec, sizeof(net_rec), &asz);
	ob_cls_obj(key);
	if ( er < E_OK ) return (INT)er;
	return (INT)( ( asz > (SZ)sizeof(net_rec) ) ? (SZ)sizeof(net_rec) : asz );
}

LOCAL void state_line( INT id, INT len, CONST char *key, CONST char *none )
{
	char	v[CF_VAL_MAX];

	if ( len > 0 && cf_get(net_rec, len, key, 0, v, sizeof(v)) && strcmp(v, "0.0.0.0") != 0 ) {
		ui_set_text(&ui, id, v);
	} else {
		ui_set_text(&ui, id, none);
	}
}

/* What the interface has now, on 〈状態〉 */
LOCAL void state_show( void )
{
	INT	len = net_read();

	if ( len <= 0 ) {
		ui_set_text(&ui, P_ST_ADDR, "（ネットワークがありません）");
		return;
	}
	state_line(P_ST_ADDR, len, "ADDRESS", "（まだありません）");
	state_line(P_ST_MASK, len, "MASK", "（なし）");
	state_line(P_ST_GW, len, "GATEWAY", "（なし）");
	state_line(P_ST_DNS, len, "DNS", "（なし）");
}

/* A lifetime as words: seconds, or "infinite" */
LOCAL void life_text( CONST char *w, char *out, INT max )
{
	INT	s;

	if ( strcmp(w, "infinite") == 0 ) {
		snprintf(out, max, "無期限");
		return;
	}
	s = cf_num(w, 0);
	if ( s >= 86400 ) snprintf(out, max, "%d日%d時間", s / 86400, s % 86400 / 3600);
	else if ( s >= 3600 ) snprintf(out, max, "%d時間%d分", s / 3600, s % 3600 / 60);
	else if ( s >= 60 ) snprintf(out, max, "%d分%d秒", s / 60, s % 60);
	else snprintf(out, max, "%d秒", s);
}

/* What is wrong with an address, from the words after its lifetimes */
LOCAL void cond_text( CONST char *v, char *out, INT max )
{
	char	w[24];
	INT	i, at = 0;

	out[0] = 0;
	for ( i = 4; cf_word(v, i, w, sizeof(w)); i++ ) {
		CONST char *s = ( strcmp(w, "tentative") == 0 ) ? "重複確認中"
			      : ( strcmp(w, "duplicated") == 0 ) ? "重複"
			      : ( strcmp(w, "deprecated") == 0 ) ? "推奨期限切れ" : NULL;

		if ( s != NULL && at < max ) at += snprintf(out + at, max - at, "・%s", s);
	}
}

#define ST6_ROWS	16

/* What IPv6 of the interface has now, on 〈ＩＰｖ６〉〈状態〉 */
LOCAL void state6_show( void )
{
	static char	rows[ST6_ROWS][96];
	CONST char	*names[ST6_ROWS];
	char		v[CF_VAL_MAX], a[64], kind[16], vl[32], pl[32], cond[64], link[96], dns[160];
	INT		len = net_read(), i, nrow = 0, at;
	BOOL		off;

	if ( len <= 0 ) {
		ui_set_text(&ui, P6_ST_MODE, "（ネットワークがありません）");
		return;
	}
	(void)cf_get(net_rec, len, "IPV6", 0, v, sizeof(v));
	off = ( v[0] == 0 || strcmp(v, "off") == 0 );
	ui_set_text(&ui, P6_ST_MODE, off ? "使用しない"
		    : ( strcmp(v, "none") == 0 ) ? "リンクローカルアドレスのみ"
		    : ( strcmp(v, "fixed") == 0 ) ? "固定"
		    : ( strcmp(v, "auto temp") == 0 ) ? "自動（ＳＬＡＡＣ）・一時アドレスあり" : "自動（ＳＬＡＡＣ）");

	strcpy(link, off ? "（使用していません）" : "（まだありません）");
	for ( i = 0; cf_get(net_rec, len, "ADDRESS6", i, v, sizeof(v)); i++ ) {
		if ( !cf_word(v, 0, a, sizeof(a)) || !cf_word(v, 1, kind, sizeof(kind)) ) continue;
		cond_text(v, cond, sizeof(cond));
		if ( strcmp(kind, "link") == 0 ) {
			snprintf(link, sizeof(link), "%s%s", a, off ? "・停止中" : cond);	/* held back while off */
			continue;
		}
		if ( off || nrow + 2 > ST6_ROWS ) continue;
		(void)cf_word(v, 2, vl, sizeof(vl));
		(void)cf_word(v, 3, pl, sizeof(pl));
		snprintf(rows[nrow++], sizeof(rows[0]), "%s", a);
		at = snprintf(rows[nrow], sizeof(rows[0]), "　%s", ( strcmp(kind, "temp") == 0 ) ? "一時"
			      : ( strcmp(kind, "auto") == 0 ) ? "自動" : "固定");
		life_text(vl, a, sizeof(a));
		at += snprintf(rows[nrow] + at, sizeof(rows[0]) - at, "・有効 %s", a);
		life_text(pl, a, sizeof(a));
		snprintf(rows[nrow] + at, sizeof(rows[0]) - at, "・推奨 %s%s", a, cond);
		nrow++;
	}
	if ( nrow == 0 ) {
		strcpy(rows[nrow++], "（ありません）");
	}
	for ( i = 0; i < nrow; i++ ) names[i] = rows[i];
	ui_set_text(&ui, P6_ST_LINK, link);
	ui_names(&ui, P6_ST_LIST, names, nrow);
	ui_set_value(&ui, P6_ST_LIST, 0);

	if ( !off && cf_get(net_rec, len, "GATEWAY6", 0, v, sizeof(v)) ) ui_set_text(&ui, P6_ST_GW, v);
	else ui_set_text(&ui, P6_ST_GW, "（なし）");
	dns[0] = 0;
	for ( i = 0, at = 0; !off && cf_get(net_rec, len, "DNS6", i, v, sizeof(v)); i++ ) {
		if ( at < (INT)sizeof(dns) ) at += snprintf(dns + at, sizeof(dns) - at, "%s%s", ( i > 0 ) ? "、" : "", v);
	}
	ui_set_text(&ui, P6_ST_DNS, ( dns[0] != 0 ) ? dns : "（なし）");
}

/* Sockets other programs hold open */
LOCAL INT busy( void )
{
	char	v[16];
	INT	len = net_read();

	return ( len > 0 && cf_get(net_rec, len, "BUSY", 0, v, sizeof(v)) ) ? cf_num(v, 0) : 0;
}

/* ---------------------------------------------------------------- the settings */

LOCAL ER conf_open( UINT ops, ID *p_key )
{
	TS_UUID	u;
	ID	key;

	if ( ui_uuid(SYSDEF_CONF_NET, &u) < E_OK ) return E_SYS;
	key = ob_opn_obj(&u, ops);
	if ( key < E_OK ) return (ER)key;
	*p_key = key;
	return E_OK;
}

LOCAL ER conf_read( void )
{
	ID	key;
	SZ	asz = 0;
	ER	er;

	er = conf_open(OB_OP_R, &key);
	if ( er < E_OK ) return er;
	er = ob_rea_rec(key, 1, 0, text, sizeof(text), &asz);
	ob_cls_obj(key);
	if ( er < E_OK ) return er;
	tlen = (INT)( ( asz > (SZ)sizeof(text) ) ? (SZ)sizeof(text) : asz );
	cf_net_read(text, tlen, &was);
	cf_net6_read(text, tlen, &was6);
	return E_OK;
}

LOCAL void to_sheet( CONST T_CFNET *n )
{
	ui_set_text(&ui, P_HOST, n->host);
	ui_set_ip(&ui, P_HOST_IP, n->addr);
	ui_set_text(&ui, P_DNS1, n->dnsname[0]);
	ui_set_ip(&ui, P_DNS1_IP, n->dns[0]);
	ui_set_text(&ui, P_DNS2, n->dnsname[1]);
	ui_set_ip(&ui, P_DNS2_IP, n->dns[1]);
	ui_set_text(&ui, P_DOM, n->domain);
	ui_set_value(&ui, P_GW, n->usegw ? 1 : 0);
	ui_set_ip(&ui, P_GW_IP, n->gw);
	if ( !n->usegw && n->gw == 0 ) ui_blank(&ui, P_GW_IP);
	ui_set_ip(&ui, P_MASK, n->mask);
}

LOCAL void from_sheet( T_CFNET *n )
{
	*n = was;			/* the time server and anything else not on the sheet */
	(void)ui_text(&ui, P_HOST, n->host, sizeof(n->host));
	n->addr = ui_get_ip(&ui, P_HOST_IP);
	(void)ui_text(&ui, P_DNS1, n->dnsname[0], sizeof(n->dnsname[0]));
	n->dns[0] = ui_get_ip(&ui, P_DNS1_IP);
	(void)ui_text(&ui, P_DNS2, n->dnsname[1], sizeof(n->dnsname[1]));
	n->dns[1] = ui_get_ip(&ui, P_DNS2_IP);
	(void)ui_text(&ui, P_DOM, n->domain, sizeof(n->domain));
	n->usegw = ( ui_value(&ui, P_GW) != 0 );
	n->gw = ui_get_ip(&ui, P_GW_IP);
	n->mask = ui_get_ip(&ui, P_MASK);
}

/* The parts of 〈ＩＰｖ６〉〈設定〉 that mean nothing as the others stand, not to be worked */
LOCAL void parts6( void )
{
	BOOL	on = ( ui_value(&ui, P6_ON) != 0 );
	INT	how = ui_value(&ui, P6_HOW);

	ui_off(&ui, P6_HOW, !on);
	ui_off(&ui, P6_TEMP, !on || how != 1);
	ui_off(&ui, P6_ADDR, !on || how != 2);
	ui_off(&ui, P6_PLEN, !on || how != 2);
	ui_off(&ui, P6_GW, !on || how != 2);
	ui_off(&ui, P6_DNS1, !on);
	ui_off(&ui, P6_DNS2, !on);
}

LOCAL void addr_to( INT id, CONST UB *a )
{
	char	s[CF_IP6_STRLEN];

	if ( cf_ip6zero(a) ) {
		ui_set_text(&ui, id, "");
	} else {
		cf_ip6str(a, s);
		ui_set_text(&ui, id, s);
	}
}

LOCAL void to_sheet6( CONST T_CFNET6 *n )
{
	INT	v = n->plen;

	ui_set_value(&ui, P6_ON, n->on ? 1 : 0);
	ui_set_value(&ui, P6_HOW, ( n->how == CF_IP6_FIXED ) ? 2 : ( n->how == CF_IP6_NONE ) ? 3 : 1);
	ui_set_value(&ui, P6_TEMP, n->temp ? 1 : 0);
	addr_to(P6_ADDR, n->addr);
	ui_set_fields(&ui, P6_PLEN, &v, 1);
	addr_to(P6_GW, n->gw);
	addr_to(P6_DNS1, n->dns[0]);
	addr_to(P6_DNS2, n->dns[1]);
	parts6();
}

/*
 * The address a box holds into a: none for an empty box. The fixed
 * address's box may carry its prefix length after a slash. FALSE when
 * what it holds is not an address.
 */
LOCAL BOOL addr_from( INT id, UB *a, INT *p_plen )
{
	char	t[64], *s = t;
	INT	i, k;

	(void)ui_text(&ui, id, t, sizeof(t));
	while ( *s == ' ' ) s++;
	for ( i = (INT)strlen(s); i > 0 && s[i - 1] == ' '; i-- ) s[i - 1] = 0;
	if ( s[0] == 0 ) {
		memset(a, 0, 16);
		return TRUE;
	}
	for ( i = 0; s[i] != 0 && s[i] != '/'; i++ ) ;
	if ( s[i] == '/' ) {
		if ( p_plen == NULL ) return FALSE;
		s[i] = 0;
		k = cf_num(s + i + 1, -1);
		if ( k < 1 || k > 128 ) return FALSE;
		*p_plen = k;
	}
	return cf_ip6(s, a);
}

/* The sheet of IPv6 into n: 0, or the number of the box that holds no address */
LOCAL INT from_sheet6( T_CFNET6 *n )
{
	INT	v[1], how, bad = 0;

	*n = was6;
	n->on = ( ui_value(&ui, P6_ON) != 0 );
	how = ui_value(&ui, P6_HOW);
	n->how = ( how == 2 ) ? CF_IP6_FIXED : ( how == 3 ) ? CF_IP6_NONE : CF_IP6_AUTO;
	n->temp = ( ui_value(&ui, P6_TEMP) != 0 );
	if ( ui_fields(&ui, P6_PLEN, v, 1) >= 1 && v[0] >= 1 && v[0] <= 128 ) n->plen = v[0];
	/* what cannot be worked as the sheet stands is not looked at: it stays as it was */
	if ( !addr_from(P6_ADDR, n->addr, &n->plen) ) {
		if ( n->on && n->how == CF_IP6_FIXED ) bad = P6_ADDR;
		memcpy(n->addr, was6.addr, 16);
		n->plen = was6.plen;
	}
	if ( !addr_from(P6_GW, n->gw, NULL) ) {
		if ( n->on && n->how == CF_IP6_FIXED && bad == 0 ) bad = P6_GW;
		memcpy(n->gw, was6.gw, 16);
	}
	if ( !addr_from(P6_DNS1, n->dns[0], NULL) ) {
		if ( n->on && bad == 0 ) bad = P6_DNS1;
		memcpy(n->dns[0], was6.dns[0], 16);
	}
	if ( !addr_from(P6_DNS2, n->dns[1], NULL) ) {
		if ( n->on && bad == 0 ) bad = P6_DNS2;
		memcpy(n->dns[1], was6.dns[1], 16);
	}
	return bad;
}

LOCAL BOOL same( CONST T_CFNET *a, CONST T_CFNET *b )
{
	return (BOOL)( strcmp(a->host, b->host) == 0 && a->addr == b->addr && a->mask == b->mask
		    && a->usegw == b->usegw && ( !a->usegw || a->gw == b->gw )
		    && strcmp(a->dnsname[0], b->dnsname[0]) == 0 && strcmp(a->dnsname[1], b->dnsname[1]) == 0
		    && a->dns[0] == b->dns[0] && a->dns[1] == b->dns[1]
		    && strcmp(a->domain, b->domain) == 0 && strcmp(a->ntp, b->ntp) == 0 );
}

LOCAL BOOL same6( CONST T_CFNET6 *a, CONST T_CFNET6 *b )
{
	return (BOOL)( memcmp(a, b, sizeof(*a)) == 0 );
}

LOCAL void warn( CONST char *line )
{
	CONST char *lines[3] = { line, "", "" };

	(void)ui_ask(&ui, PN_WARN, lines, 3);
}

/* A name of letters, digits and signs only: what a host name and a domain may be */
LOCAL BOOL plain( CONST char *s )
{
	INT	i;

	for ( i = 0; s[i] != 0; i++ ) {
		if ( (UB)s[i] < 0x21 || (UB)s[i] > 0x7E ) return FALSE;
	}
	return TRUE;
}

/*
 * The sheet written as the settings and taken by the system. A network
 * in use by other programs is said so first, and may be given up.
 */
LOCAL BOOL write_out( CONST T_CFNET *n, CONST T_CFNET6 *n6 )
{
	ID	key;
	SZ	asz = 0;
	INT	len;
	ER	er, adm = E_NOEXS;

	if ( busy() > 0 && ui_ask(&ui, PN_BUSY, NULL, 0) != B_UPDATE ) {
		return FALSE;
	}
	len = cf_net_write(text, tlen, (INT)sizeof(text), n);
	if ( len >= 0 ) len = cf_net6_write(text, len, (INT)sizeof(text), n6);
	er = ( len < 0 ) ? E_LIMIT : conf_open(OB_OP_R | OB_OP_WRITE, &key);
	if ( er == E_OACV ) {
		/* the network's settings are the administrator's: the password asked for, by the desktop */
		adm = ts_admin_ask("ネットワーク設定の変更", &ui.win);
		er = ( adm >= E_OK ) ? conf_open(OB_OP_R | OB_OP_WRITE, &key) : E_OACV;
	}
	if ( er >= E_OK ) {
		er = ob_wri_rec(key, 1, 0, text, len, &asz);
		if ( er >= E_OK ) er = ob_trn_rec(key, 1, (UD)len);
		ob_cls_obj(key);
	}
	if ( adm >= E_OK ) (void)ob_end_adm();
	if ( er == E_OACV ) {
		if ( adm != E_ABORT ) {
			CONST char *lines[3] = { "ネットワーク設定を変更する権限がありません。",
						 "管理者のパスワードが必要です。", "" };

			(void)ui_ask(&ui, PN_WARN, lines, 3);
		}
		tm_putstring((CONST UB *)"netenv: not written: no right\n");
		return FALSE;
	}
	if ( er < E_OK ) {
		char	why[64];
		CONST char *lines[3] = { "ネットワーク設定が保存できません。", why, "" };

		snprintf(why, sizeof(why), "（エラー %d）", (INT)er);
		(void)ui_ask(&ui, PN_WARN, lines, 3);
		return FALSE;
	}
	tlen = len;
	was = *n;
	was6 = *n6;
	to_sheet6(&was6);			/* the addresses as the record has them */
	tm_putstring((CONST UB *)"netenv: written\n");
	(void)tk_dly_tsk(500);			/* the system takes it as it sees it written */
	state_show();
	state6_show();
	return TRUE;
}

/* An address a machine may be given: not unspecified, loopback, link-local or multicast */
LOCAL BOOL unicast6( CONST UB *a )
{
	static CONST UB	lo[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 };

	return (BOOL)!( cf_ip6zero(a) || memcmp(a, lo, 16) == 0 || a[0] == 0xff
		       || ( a[0] == 0xfe && ( a[1] & 0xc0 ) == 0x80 ) );
}

/* The sheet looked over before it is written: FALSE when something is wrong with it */
LOCAL BOOL check( CONST T_CFNET *n, CONST T_CFNET6 *n6, INT bad6 )
{
	if ( !plain(n->host) || !plain(n->dnsname[0]) || !plain(n->dnsname[1]) || !plain(n->domain) ) {
		warn("英数記号以外の文字は使用できません。");
		return FALSE;
	}
	if ( n->addr != 0 && n->mask == 0 ) {
		warn("サブネットマスクを設定してください。");
		return FALSE;
	}
	if ( bad6 != 0 ) {
		ui_show_sheet(&ui, 3);
		warn("ＩＰｖ６アドレスの書き方が正しくありません。");
		return FALSE;
	}
	if ( n6->on && n6->how == CF_IP6_FIXED && !unicast6(n6->addr) ) {
		ui_show_sheet(&ui, 3);
		warn(cf_ip6zero(n6->addr) ? "固定のＩＰｖ６アドレスを設定してください。"
					  : "このアドレスは固定アドレスに使用できません。");
		return FALSE;
	}
	return TRUE;
}

/* 設定を更新 [S]: written at once, the window left open */
LOCAL void update( void )
{
	T_CFNET		n;
	T_CFNET6	n6;
	INT		bad6;

	from_sheet(&n);
	bad6 = from_sheet6(&n6);
	if ( !check(&n, &n6, bad6) ) return;
	if ( same(&n, &was) && same6(&n6, &was6) ) {
		warn("内容は変更されていません。");
		return;
	}
	if ( ui_ask(&ui, PN_UPDATE, NULL, 0) != B_UPDATE ) return;
	(void)write_out(&n, &n6);
}

/* 終了 [E]: asked about what was changed, then closed */
LOCAL void finish( void )
{
	T_CFNET		n;
	T_CFNET6	n6;
	INT		a, bad6;

	from_sheet(&n);
	bad6 = from_sheet6(&n6);
	if ( bad6 == 0 && same(&n, &was) && same6(&n6, &was6) ) {
		closing = TRUE;
		return;
	}
	a = ui_ask(&ui, PN_FINISH, NULL, 0);
	if ( a == B_NOSAVE ) {
		closing = TRUE;
	} else if ( a == B_SAVE && check(&n, &n6, bad6) && write_out(&n, &n6) ) {
		closing = TRUE;
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

	if ( ui_uuid(SYSDEF_MENU_NETENV, &def) < E_OK || mn_cre_men(&def, &mid) < E_OK ) return;
	if ( ui.last == 0 ) {
		(void)mn_chg_atr(mid, NULL, "clear", MN_GREY);
	}
	chosen = (BOOL)( mn_pop_men(mid, ui.kw, m->x, m->y, m->when, &sel) >= E_OK );
	(void)mn_del_men(mid);
	if ( !chosen ) return;
	if ( is_code(&sel, "close") ) {
		finish();
	} else if ( is_code(&sel, "update") ) {
		update();
	} else if ( is_code(&sel, "clear") && ui.last != 0 ) {
		/* what was typed into last, cleared */
		if ( ui.last == P_HOST || ui.last == P_DNS1 || ui.last == P_DNS2 || ui.last == P_DOM
		  || ui.last == P6_ADDR || ui.last == P6_GW || ui.last == P6_DNS1 || ui.last == P6_DNS2 ) {
			ui_set_text(&ui, ui.last, "");
		} else if ( ui.last == P6_PLEN ) {
			INT	v = 64;

			ui_set_fields(&ui, P6_PLEN, &v, 1);
		} else {
			ui_set_ip(&ui, ui.last, 0);
		}
	}
}

LOCAL void key( CONST T_OBNTM *m )
{
	UB	c = wm_key_char(m->code, m->mods);

	if ( c == 'e' || c == 'E' ) finish();
	else if ( c == 's' || c == 'S' ) update();
}

int main( void )
{
	T_OBNTM	m;
	SZ	asz = 0;
	INT	id, ev;

	ui_icon(SYSDEF_PROG_NETENV);
	if ( ui_open(&ui, SYSDEF_BOX_NETENV, PN_MAIN, 200, 120) < E_OK ) {
		tm_putstring((CONST UB *)"netenv: no window\n");
		return 1;
	}
	if ( conf_read() < E_OK ) {
		warn("ネットワーク設定が読み込めません。");
		ui_close(&ui);
		return 2;
	}
	to_sheet(&was);
	to_sheet6(&was6);
	state_show();
	state6_show();
	tm_putstring((CONST UB *)"netenv: ready\n");

	while ( !closing ) {
		while ( !closing && ob_rea_rec(ui.port, 0, 0, &m, sizeof(m), &asz) >= E_OK ) {
			ev = ui_event(&ui, &m, &id);
			switch ( ev ) {
			case UI_EV_CLOSE:
				if ( m.event == OB_E_DELETE ) closing = TRUE;
				else finish();
				break;
			case UI_EV_MENU:
				menu(&m);
				break;
			case UI_EV_KEY:
				if ( m.mods & ( 0x01 | 0x10 ) ) key(&m);
				break;
			case UI_EV_BUTTON:
				if ( id == P_ST_AGAIN ) state_show();
				if ( id == P6_ST_AGAIN ) state6_show();
				break;
			case UI_EV_SHEET:
				if ( id == TAG_STATE ) state_show();
				if ( id == TAG_STATE6 ) state6_show();
				break;
			case UI_EV_CHANGE:
				if ( id == P_GW_IP && ui_value(&ui, P_GW) == 0 ) {
					ui_set_value(&ui, P_GW, 1);	/* an address given: the gateway is used */
				}
				if ( id == P6_ON || id == P6_HOW ) parts6();
				break;
			default:
				break;
			}
		}
		if ( !closing ) {
			(void)tk_dly_tsk(TICK_MS);
		}
	}
	ui_close(&ui);
	tm_putstring((CONST UB *)"netenv: closed\n");
	return 0;
}
