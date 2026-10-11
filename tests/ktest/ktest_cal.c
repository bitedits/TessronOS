/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_cal.c
 *	予定表 (design 11.10): the records it reads and writes
 *
 *	The month record is the one TADjs Desktop keeps for the same month
 *	and the same link, byte for byte: its length and its FNV-1a hash
 *	are those of the record of January 2026 with the holidays 元日 and
 *	成人の日 and a link to 2026/01/05 as TADjs Desktop writes it.
 *	The holidays are read from the 休日 object as TADjs Desktop writes it
 *	and as a text edited here writes it, and back from a month record;
 *	a link keeps every attribute it came with; the day's text is made
 *	from the template. A link's attributes are set, taken off and read
 *	as text; a link sized is closed or open by its height; what is
 *	taken goes to the tray as a figure of links; an applist's program
 *	is kept to and let go.
 */

#include "ktest.h"
#include "tstdlib.h"
#include "../../application/calendar/cal_tad.h"

#define OUT_MAX		( 64 * 1024 )

LOCAL INT s_len( CONST char *s )
{
	INT	n = 0;

	while ( s[n] != 0 ) n++;
	return n;
}

LOCAL BOOL has( CONST UB *s, INT n, CONST char *w )
{
	INT	i, k, m = s_len(w);

	for ( i = 0; i + m <= n; i++ ) {
		for ( k = 0; k < m && s[i + k] == (UB)w[k]; k++ ) ;
		if ( k == m ) return TRUE;
	}
	return FALSE;
}

LOCAL BOOL str_eq( CONST UB *a, CONST char *b )
{
	INT	i;

	for ( i = 0; b[i] != 0; i++ ) {
		if ( a[i] != (UB)b[i] ) return FALSE;
	}
	return (BOOL)( a[i] == 0 );
}

LOCAL UW fnv( CONST UB *s, INT n )
{
	UW	h = 0x811C9DC5U;
	INT	i;

	for ( i = 0; i < n; i++ ) h = ( h ^ s[i] ) * 0x01000193U;
	return h;
}

/* the calendar's arithmetic */
LOCAL void test_dates( void )
{
	INT	y = 0, m = 0, d = 0, row = 0, col = 0, box[4];
	UB	s[12];

	KT_ASSERT_EQ(ct_days(2026, 2), 28);
	KT_ASSERT_EQ(ct_days(2024, 2), 29);
	KT_ASSERT_EQ(ct_days(2000, 2), 29);
	KT_ASSERT_EQ(ct_days(1900, 2), 28);
	KT_ASSERT_EQ(ct_days(2026, 12), 31);
	KT_ASSERT_EQ(ct_wday(2026, 1, 1), 4);		/* Thursday */
	KT_ASSERT_EQ(ct_wday(2026, 10, 1), 4);
	KT_ASSERT_EQ(ct_wday(2026, 2, 1), 0);		/* Sunday */
	KT_ASSERT_EQ(ct_wday(2024, 2, 29), 4);

	KT_ASSERT(ct_is_day((CONST UB *)"2026/01/05", &y, &m, &d));
	KT_ASSERT_EQ(y, 2026);
	KT_ASSERT_EQ(m, 1);
	KT_ASSERT_EQ(d, 5);
	KT_ASSERT(!ct_is_day((CONST UB *)"2026/1/05", NULL, NULL, NULL));
	KT_ASSERT(!ct_is_day((CONST UB *)"2026/13/05", NULL, NULL, NULL));
	KT_ASSERT(!ct_is_day((CONST UB *)"2026/01/05 会議", NULL, NULL, NULL));
	KT_ASSERT(ct_is_month((CONST UB *)"2026/10", &y, &m));
	KT_ASSERT_EQ(m, 10);
	KT_ASSERT(!ct_is_month((CONST UB *)"2026/10/01", NULL, NULL));
	ct_day_name(s, 2026, 3, 7);
	KT_ASSERT(str_eq(s, "2026/03/07"));
	ct_month_name(s, 987, 11);
	KT_ASSERT(str_eq(s, "0987/11"));

	/* 2026/01/05 is the Monday of the second row */
	ct_cell(2026, 1, 5, &row, &col);
	KT_ASSERT_EQ(row, 1);
	KT_ASSERT_EQ(col, 1);
	ct_day_box(2026, 1, 5, 1, box);
	KT_ASSERT_EQ(box[0], 116);
	KT_ASSERT_EQ(box[1], 198);
	KT_ASSERT_EQ(box[2], 226);
	KT_ASSERT_EQ(box[3], 216);
}

/* 休日: as TADjs Desktop writes it, and as a text edited here may write it */
LOCAL void test_holidays( void )
{
	static CONST char	tadjs[] =
		"<tad version=\"1.0\" encoding=\"UTF-8\" filename=\"休日\">\r\n<document>\r\n<p>\r\n"
		"2026/01/01\t元日<br/>\r\n2026/01/12\t成人の日<br/>\r\n"
		"2026/10/12\tスポーツの日<br/>\r\n2026/11/23\t勤労感謝の日<br/>\r\n</p>\r\n</document>\r\n</tad>\r\n";
	static CONST char	edited[] =
		"<tad version=\"1.0\" encoding=\"UTF-8\"><document><text lang=\"0\"/><p>2026/05/03<tab/>憲法記念日"
		"<br/>2026/05/04\t &amp;みどりの日　</p><p><font size=\"14\"/>2026/05/05\tこどもの日</p>"
		"<p>覚え書き</p></document></tad>";
	CTHOL	h[8];
	INT	n;

	n = ct_hol_text((CONST UB *)tadjs, s_len(tadjs), h, 8);
	KT_ASSERT_EQ(n, 4);
	KT_ASSERT(ct_hol_find(h, n, 2026, 1, 1) != NULL && str_eq(ct_hol_find(h, n, 2026, 1, 1), "元日"));
	KT_ASSERT(str_eq(ct_hol_find(h, n, 2026, 10, 12), "スポーツの日"));
	KT_ASSERT(ct_hol_find(h, n, 2026, 1, 2) == NULL);

	n = ct_hol_text((CONST UB *)edited, s_len(edited), h, 8);
	KT_ASSERT_EQ(n, 3);
	KT_ASSERT(str_eq(ct_hol_find(h, n, 2026, 5, 3), "憲法記念日"));
	KT_ASSERT(str_eq(ct_hol_find(h, n, 2026, 5, 4), "&みどりの日"));	/* the ideographic space left off */
	KT_ASSERT(str_eq(ct_hol_find(h, n, 2026, 5, 5), "こどもの日"));

	/* no more than there is room for */
	KT_ASSERT_EQ(ct_hol_text((CONST UB *)tadjs, s_len(tadjs), h, 2), 2);
}

/* a month written as TADjs Desktop writes it, and read back */
LOCAL void test_month( void )
{
	UB	*out = (UB *)Kmalloc(OUT_MAX), el[1024];
	CTBUF	b, e;
	CTHOL	h[4], back[4];
	CTLNK	l[2];
	CTEL	links[4];
	TS_UUID	t, v, u;
	INT	box[4] = { 0, 0, 0, 0 }, n;

	if ( out == NULL ) KT_SKIP("no memory");
	knl_memset(h, 0, sizeof(h));
	h[0].y = 2026; h[0].m = 1; h[0].d = 1;
	knl_strcpy((char *)h[0].name, "元日");
	h[1].y = 2026; h[1].m = 1; h[1].d = 12;
	knl_strcpy((char *)h[1].name, "成人の日");
	KT_ASSERT(ct_uuid_parse((CONST UB *)"019d0000-0000-7000-8000-0000000000aa", &t));
	KT_ASSERT(ct_uuid_parse((CONST UB *)"019d0000-0000-7000-8000-000000000001", &v));
	ct_buf(&e, el, sizeof(el));
	ct_link_new(&e, &t, &v, box, TRUE);
	l[0].el = e.p;
	l[0].len = e.n;
	l[0].name = (CONST UB *)"2026/01/05";
	ct_buf(&b, out, OUT_MAX);
	KT_ASSERT_ER(ct_month_xml(&b, 2026, 1, 0, h, 2, l, 1), E_OK);
	tm_printf((UB *)"  month record: %d bytes, %08x\n", b.n, fnv(b.p, b.n));
	KT_ASSERT_EQ(b.n, 22148);
	KT_ASSERT_EQ(fnv(b.p, b.n), 0xFA5DF6C3U);
	KT_ASSERT(has(b.p, b.n, "vobjleft=\"116\" vobjtop=\"178\" vobjright=\"226\" vobjbottom=\"196\" height=\"18\""));
	KT_ASSERT(has(b.p, b.n, "<docmemo text=\"holiday\"/>成人の日\n"));

	/* the holidays and the link read back from it */
	n = ct_hol_month(b.p, b.n, 2026, 1, back, 4);
	KT_ASSERT_EQ(n, 2);
	KT_ASSERT(str_eq(ct_hol_find(back, n, 2026, 1, 1), "元日"));
	KT_ASSERT(str_eq(ct_hol_find(back, n, 2026, 1, 12), "成人の日"));
	n = ct_links(b.p, b.n, links, 4);
	KT_ASSERT_EQ(n, 1);
	KT_ASSERT(ct_target(b.p + links[0].at, links[0].len, &u));
	KT_ASSERT_EQ(ts_uuid_cmp(&u, &t), 0);
	KT_ASSERT_EQ(ct_attr_int(b.p + links[0].at, links[0].len, "zIndex", -1), 200);

	/* today's cell, and a second link of the day under the first */
	l[1] = l[0];
	ct_buf(&b, out, OUT_MAX);
	KT_ASSERT_ER(ct_month_xml(&b, 2026, 1, 5, h, 2, l, 2), E_OK);
	KT_ASSERT(has(b.p, b.n, "f_pat=\"133\" l_pat=\"130\" left=\"114\" top=\"158\""));
	KT_ASSERT(has(b.p, b.n, "vobjtop=\"198\" vobjright=\"226\" vobjbottom=\"216\""));

	/* too small a buffer says so */
	ct_buf(&b, out, 1000);
	KT_ASSERT_ER(ct_month_xml(&b, 2026, 1, 0, h, 2, l, 1), E_LIMIT);
	Kfree(out);
}

/* a link written again keeps what it had; a day's gets the calendar's look when it has none */
LOCAL void test_link( void )
{
	static CONST char	in[] = "<link id=\"019d0000-0000-7000-8000-0000000000bb_0.xtad\" vobjleft=\"5\""
				       " vobjtop='6' relationship=\"会議 定例\" applist=\"{&quot;a&quot;:1}\""
				       " zIndex=\"3\" frcol=\"\">2026/01/07</link>";
	UB	out[1024], val[64];
	CTBUF	b;
	CTEL	el[2];
	INT	box[4] = { 10, 20, 110, 38 };

	KT_ASSERT_EQ(ct_links((CONST UB *)in, s_len(in), el, 2), 1);
	KT_ASSERT_EQ(el[0].len, s_len(in));		/* the words and the end tag with it */
	ct_buf(&b, out, sizeof(out));
	ct_link_put(&b, (CONST UB *)in, s_len(in), box, 205, TRUE);
	KT_ASSERT(!b.fail);
	KT_ASSERT(has(b.p, b.n, "vobjleft=\"10\" vobjtop=\"20\""));
	KT_ASSERT(has(b.p, b.n, " relationship=\"会議 定例\" applist=\"{&quot;a&quot;:1}\" zIndex=\"205\""));
	KT_ASSERT(has(b.p, b.n, "frcol=\"#b0c0d0\""));
	KT_ASSERT(has(b.p, b.n, "vobjright=\"110\" vobjbottom=\"38\" height=\"18\" chsz=\"10\""));
	KT_ASSERT(b.p[b.n - 2] == '/' && b.p[b.n - 1] == '>');
	KT_ASSERT_EQ(ct_attr(b.p, b.n, "chcol", val, sizeof(val)), 7);
	KT_ASSERT(str_eq(val, "#000000"));
	KT_ASSERT_EQ(ct_attr(b.p, b.n, "tbcol", val, sizeof(val)), 7);
	KT_ASSERT(str_eq(val, "#e8f4ff"));
	KT_ASSERT_EQ(ct_attr(b.p, b.n, "hidden", val, sizeof(val)), -1);
}

/* a month's link put in the 予定表 object, and a day's text from the template */
LOCAL void test_records( void )
{
	static CONST char	cal[] =
		"<tad version=\"1.0\" encoding=\"UTF-8\" filename=\"予定表\">\n<figure>\n"
		"<link id=\"019b8230-f63a-7985-b69d-20e43d55506c_0.xtad\" vobjleft=\"10\"/>\n</figure>\n</tad>\n";
	static CONST char	tpl[] =
		"<tad version=\"1.0\" encoding=\"UTF-8\">\n<document>\n<p>\n<text align=\"center\"/>"
		"<font size=\"21\">YYYY/MM/DD</font>\n</p>\n<p>□TODO YYYY/MM/DD</p>\n</document>\n</tad>\n";
	static CONST char	named[] = "<tad version=\"1.0\" filename=\"年月日実身\" encoding=\"UTF-8\"><document/></tad>";
	UB	out[1024], el[600];
	CTBUF	b, e;
	CTEL	links[4];
	TS_UUID	t, v;
	INT	box[4] = { 10, 90, 160, 115 };

	KT_ASSERT(ct_uuid_parse((CONST UB *)"019d0000-0000-7000-8000-0000000000cc", &t));
	KT_ASSERT(ct_uuid_parse((CONST UB *)"019d0000-0000-7000-8000-000000000002", &v));
	ct_buf(&e, el, sizeof(el));
	ct_link_new(&e, &t, &v, box, FALSE);
	KT_ASSERT(has(e.p, e.n, "height=\"25\" chsz=\"14\" frcol=\"#000000\" chcol=\"#000000\" tbcol=\"#ffffff\""));
	ct_buf(&b, out, sizeof(out));
	KT_ASSERT_ER(ct_insert(&b, (CONST UB *)cal, s_len(cal), e.p, e.n), E_OK);
	KT_ASSERT_EQ(ct_links(b.p, b.n, links, 4), 2);
	KT_ASSERT(has(b.p, b.n, "zoomratio=\"1\"/>\n</figure>\n</tad>\n"));

	ct_buf(&b, out, sizeof(out));
	ct_apply_date(&b, (CONST UB *)tpl, s_len(tpl), (CONST UB *)"2026/10/05");
	KT_ASSERT(!has(b.p, b.n, "YYYY"));
	KT_ASSERT(has(b.p, b.n, "<font size=\"21\">2026/10/05</font>"));
	KT_ASSERT(has(b.p, b.n, "□TODO 2026/10/05"));
	ct_buf(&b, out, sizeof(out));
	ct_apply_date(&b, (CONST UB *)named, s_len(named), (CONST UB *)"2026/10/05");
	KT_ASSERT(has(b.p, b.n, "filename=\"2026/10/05\" encoding"));
	ct_buf(&b, out, sizeof(out));
	ct_day_doc(&b, (CONST UB *)"2026/10/06");
	KT_ASSERT(has(b.p, b.n, "<font size=\"20\"/>\n<text align=\"center\"/>\n2026/10/06\n</p>"));
}

/* a link's attributes set, taken off and added, and read as text */
LOCAL void test_edit( void )
{
	static CONST char	in[] = "<link id=\"019d0000-0000-7000-8000-0000000000bb_0.xtad\" vobjleft=\"5\""
				       " relationship=\"会議 &amp; 定例\" chsz=\"10\" hidden=\"false\">2026/01/07</link>";
	CTSET	set[4];
	UB	out[1024], val[64];
	CTBUF	b;

	set[0].name = "chsz";		set[0].value = (CONST UB *)"14";
	set[1].name = "hidden";		set[1].value = NULL;
	set[2].name = "autoopen";	set[2].value = (CONST UB *)"true";
	set[3].name = "applist";	set[3].value = (CONST UB *)"{\"a\":\"<b>\"}";
	ct_buf(&b, out, sizeof(out));
	ct_link_edit(&b, (CONST UB *)in, s_len(in), set, 4);
	KT_ASSERT(!b.fail);
	KT_ASSERT(has(b.p, b.n, " vobjleft=\"5\" relationship=\"会議 &amp; 定例\" chsz=\"14\""));
	KT_ASSERT(!has(b.p, b.n, "hidden"));
	KT_ASSERT(has(b.p, b.n, " autoopen=\"true\" applist=\"{&quot;a&quot;:&quot;&lt;b&gt;&quot;}\"/>"));
	KT_ASSERT(!has(b.p, b.n, "</link>"));

	/* read back as text, the escapes undone */
	KT_ASSERT_EQ(ct_attr_text((CONST UB *)in, s_len(in), "relationship", val, sizeof(val)), 15);
	KT_ASSERT(str_eq(val, "会議 & 定例"));
	KT_ASSERT_EQ(ct_attr_text(b.p, b.n, "applist", val, sizeof(val)), 11);
	KT_ASSERT(str_eq(val, "{\"a\":\"<b>\"}"));
	KT_ASSERT_EQ(ct_attr_text(b.p, b.n, "hidden", val, sizeof(val)), -1);
	KT_ASSERT_EQ(ct_attr_text((CONST UB *)in, s_len(in), "relationship", val, 5), 3);	/* not in a letter */
	KT_ASSERT(str_eq(val, "会"));
	KT_ASSERT(!ct_attr_bool((CONST UB *)in, s_len(in), "hidden", TRUE));
	KT_ASSERT(ct_attr_bool(b.p, b.n, "autoopen", FALSE));
	KT_ASSERT(ct_attr_bool(b.p, b.n, "pictdisp", TRUE));
}

/* a link sized: closed or open by its height; a box kept inside the month */
LOCAL void test_box( void )
{
	INT	b1[4] = { 700, 50, 850, 75 }, b2[4] = { -5, 590, 145, 615 }, b3[4] = { 10, 100, 900, 800 };

	KT_ASSERT_EQ(ct_band_h(14), 25);
	KT_ASSERT_EQ(ct_band_h(0), 25);
	KT_ASSERT_EQ(ct_band_h(10), 21);
	KT_ASSERT_EQ(ct_snap_h(14, 30), 25);
	KT_ASSERT_EQ(ct_snap_h(14, 33), 25);
	KT_ASSERT_EQ(ct_snap_h(14, 34), 34);
	KT_ASSERT_EQ(ct_snap_h(10, 12), 21);
	ct_fit_box(b1);
	KT_ASSERT(b1[0] == 650 && b1[1] == 70 && b1[2] == 800 && b1[3] == 95);
	ct_fit_box(b2);
	KT_ASSERT(b2[0] == 0 && b2[1] == 575 && b2[2] == 150 && b2[3] == 600);
	ct_fit_box(b3);
	KT_ASSERT(b3[0] == 0 && b3[1] == CT_GRID_TOP && b3[2] == 800 && b3[3] == 600);
}

/* what is taken, as the figure it goes to the tray as, and read back from it */
LOCAL void test_frag( void )
{
	static CONST char	a[] = "<link id=\"019d0000-0000-7000-8000-0000000000aa_0.xtad\" vobjleft=\"1\" vobjtop=\"80\""
				      " vobjright=\"151\" vobjbottom=\"105\">2026/01/05</link>";
	static CONST char	c[] = "<link id=\"019d0000-0000-7000-8000-0000000000cc_0.xtad\" chsz=\"10\"/>";
	UB	out[1024];
	CTBUF	b;
	CTLNK	l[2];
	CTEL	el[4];
	TS_UUID	u, t;

	l[0].el = (CONST UB *)a;
	l[0].len = s_len(a);
	l[0].name = (CONST UB *)"2026/01/05";
	l[1].el = (CONST UB *)c;
	l[1].len = s_len(c);
	l[1].name = (CONST UB *)"会議";
	ct_buf(&b, out, sizeof(out));
	KT_ASSERT_ER(ct_frag(&b, l, 2), E_OK);
	KT_ASSERT(has(b.p, b.n, "<tad version=\"1.0\" encoding=\"UTF-8\">\n<figure>\n<link "));
	KT_ASSERT(has(b.p, b.n, "</figure>\n</tad>\n"));
	KT_ASSERT_EQ(ct_links(b.p, b.n, el, 4), 2);
	KT_ASSERT_EQ(ct_attr_int(b.p + el[0].at, el[0].len, "vobjtop", -1), 80);
	KT_ASSERT(ct_target(b.p + el[1].at, el[1].len, &u));
	KT_ASSERT(ct_uuid_parse((CONST UB *)"019d0000-0000-7000-8000-0000000000cc", &t));
	KT_ASSERT_EQ(ts_uuid_cmp(&u, &t), 0);
	ct_buf(&b, out, 40);
	KT_ASSERT_ER(ct_frag(&b, l, 2), E_LIMIT);
}

/* an applist: the program the object opens with kept to and let go, its entries listed; a colour of metadata */
LOCAL void test_apps( void )
{
	static CONST char	apps[] = "{\"basic-text-editor\":{\"name\":\"基本文章編集\",\"defaultOpen\":true},"
					 "\"basic-figure-editor\":{\"name\":\"基本図形編集\",\"defaultOpen\":false}}";
	static CONST char	none[] = "{\"x\":{\"name\":\"X\"}}";
	static CONST char	meta[] = "{\"name\":\"予定表\",\"window\":{\"width\":800,\"backgroundColor\":\"#dedede\"}}";
	UB	out[512], back[512], ids[4][CT_APP_ID], names[4][CT_APP_NAME];
	CTBUF	b, c;

	KT_ASSERT(!ct_app_locked((CONST UB *)apps, s_len(apps)));
	ct_buf(&b, out, sizeof(out));
	KT_ASSERT_ER(ct_app_lock(&b, (CONST UB *)apps, s_len(apps), TRUE), E_OK);
	KT_ASSERT(has(b.p, b.n, "\"defaultOpen\":true,\"defaultOpenLock\":true},\"basic-figure-editor\""));
	KT_ASSERT(ct_app_locked(b.p, b.n));
	ct_buf(&c, back, sizeof(back));
	KT_ASSERT_ER(ct_app_lock(&c, b.p, b.n, FALSE), E_OK);
	KT_ASSERT(has(c.p, c.n, "\"defaultOpenLock\":false}"));
	KT_ASSERT_EQ(c.n, b.n + 1);
	KT_ASSERT(!ct_app_locked(c.p, c.n));
	ct_buf(&b, out, sizeof(out));
	KT_ASSERT_ER(ct_app_lock(&b, (CONST UB *)none, s_len(none), TRUE), E_NOEXS);

	KT_ASSERT_EQ(ct_app_list((CONST UB *)apps, s_len(apps), ids, names, 4), 2);
	KT_ASSERT(str_eq(ids[0], "basic-text-editor"));
	KT_ASSERT(str_eq(names[0], "基本文章編集"));
	KT_ASSERT(str_eq(ids[1], "basic-figure-editor"));
	KT_ASSERT_EQ(ct_app_list((CONST UB *)apps, s_len(apps), ids, names, 1), 1);

	KT_ASSERT_EQ(ct_json_colour((CONST UB *)meta, s_len(meta), "window", "backgroundColor", 0), 0xDEDEDEU);
	KT_ASSERT_EQ(ct_json_colour((CONST UB *)meta, s_len(meta), "window", "foreground", 7), 7U);
	KT_ASSERT_EQ(ct_json_colour((CONST UB *)none, s_len(none), "window", "backgroundColor", 9), 9U);
}

EXPORT void ktest_cal( void )
{
	tm_printf((UB *)"\n[cal] 予定表 (design 11.10)\n");
	KT_RUN(test_dates);
	KT_RUN(test_holidays);
	KT_RUN(test_month);
	KT_RUN(test_link);
	KT_RUN(test_records);
	KT_RUN(test_edit);
	KT_RUN(test_box);
	KT_RUN(test_frag);
	KT_RUN(test_apps);
}
