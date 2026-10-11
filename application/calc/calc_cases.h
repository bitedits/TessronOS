/*
 * calc_cases.h -- made by tools/calc_cases.js; not to be edited.
 * What each cell of the sheets shows, as the formula methods of the
 * editor and formula.min.js work it out.
 */

typedef struct { const char *cell, *set, *shows; } CALC_CASE;
typedef struct { int c; int n; const CALC_CASE *cells; } CALC_SHEET;
typedef struct { const char *in, *out; } CALC_OLD;
typedef struct { const char *in; int dc, dr; const char *out; } CALC_ADJ;

static const CALC_CASE calc_sheet0[] = {
	{ "A1", "10", "10" },
	{ "A2", "20", "20" },
	{ "A3", "30", "30" },
	{ "B1", "=A1+A2", "30" },
	{ "B2", "=A1*A2-A3", "170" },
	{ "B3", "=(A1+A2)/A3", "1" },
	{ "B4", "=A3/7", "4.285714285714286" },
	{ "B5", "=A1%3", "1" },
	{ "B6", "=2**10", "1024" },
	{ "B7", "=-A1+5", "-5" },
	{ "B8", "=0.1+0.2", "0.30000000000000004" },
	{ "B9", "=1/3", "0.3333333333333333" },
	{ "B10", "=A1/0", "Infinity" },
	{ "B11", "=0/0", "" },
	{ "B12", "=A1-A1", "" },
	{ "B13", "=1e21*10", "1e+22" },
	{ "B14", "=123456789*1000", "123456789000" },
	{ "B15", "=2.5*4", "10" },
	{ "B16", "=7/2", "3.5" },
};
static const CALC_CASE calc_sheet1[] = {
	{ "A1", "10", "10" },
	{ "A2", "20", "20" },
	{ "A3", "30", "30" },
	{ "C1", "=SUM(A1:A3)", "60" },
	{ "C2", "=AVERAGE(A1:A3)", "20" },
	{ "C3", "=COUNT(A1:B3)", "3" },
	{ "C4", "=MAX(A1:A3)", "30" },
	{ "C5", "=MIN(A1:A3)", "10" },
	{ "C6", "=SUM(A1:A3)*2", "120" },
	{ "C7", "=MAX(A1:A3,100)", "100" },
	{ "C8", "=SUM(A1,A2,5)", "35" },
	{ "C9", "=AVERAGE(E1:E3)", "Error: #DIV/0!" },
};
static const CALC_CASE calc_sheet2[] = {
	{ "A1", "2.345", "2.345" },
	{ "A2", "-2.5", "-2.5" },
	{ "A3", "3.7", "3.7" },
	{ "B1", "=ROUND(A1,2)", "2.35" },
	{ "B2", "=ROUND(A2,0)", "-2" },
	{ "B3", "=ROUND(A3)", "4" },
	{ "B4", "=FLOOR(A3,1)", "3" },
	{ "B5", "=CEIL(3.2,1)", "4" },
	{ "B6", "=ABS(A2)", "2.5" },
	{ "B7", "=SQRT(16)", "4" },
	{ "B8", "=POWER(2,0.5)", "1.4142135623730951" },
	{ "B9", "=MOD(10,3)", "1" },
	{ "B10", "=MOD(-7,3)", "2" },
	{ "B11", "=PI()", "3.141592653589793" },
	{ "B12", "=ROUND(PI(),4)", "3.1416" },
	{ "B13", "=SQRT(-1)", "Error: #NUM!" },
	{ "B14", "=MOD(5,0)", "Error: #DIV/0!" },
	{ "B15", "=ROUND(1234.5678,-2)", "1200" },
};
static const CALC_CASE calc_sheet3[] = {
	{ "A1", "8", "8" },
	{ "A2", "abc", "abc" },
	{ "A3", "", "" },
	{ "B1", "=IF(A1>5,\"big\",\"small\")", "big" },
	{ "B2", "=IF(A1<5,1,2)", "2" },
	{ "B3", "=A1>5", "true" },
	{ "B4", "=A1<5", "" },
	{ "B5", "=A2", "abc" },
	{ "B6", "=A1+A2", "8abc" },
	{ "B7", "=\"x\"+\"y\"", "xy" },
	{ "B8", "=A3+1", "1" },
	{ "B9", "=A1==8", "true" },
	{ "B10", "=A1>=8&&A1<=9", "true" },
	{ "B11", "=A1!=8||A1>100", "" },
	{ "B12", "=A1>5?\"yes\":\"no\"", "yes" },
	{ "B13", "=!(A1>5)", "" },
	{ "B14", "=A2+1", "abc1" },
};
static const CALC_CASE calc_sheet4[] = {
	{ "A1", "1", "5" },
	{ "A2", "=A1+1", "6" },
	{ "A3", "=A2*10", "60" },
	{ "A4", "=A3+A2+A1", "71" },
	{ "A1", "5", "5" },
	{ "B1", "=B1+1", "#CIRCULAR!" },
	{ "B2", "=SUM(B1:B3)", "#CIRCULAR!" },
	{ "B3", "=C1", "7" },
	{ "C1", "7", "7" },
	{ "B4", "=[1,1]+[2,1]", "11" },
	{ "B5", "=\357\274\2731\357\274\2141\357\274\275*2", "10" },
	{ "B6", "=SUM(B5:B6)", "#CIRCULAR!" },
};
static const CALC_CASE calc_sheet5[] = {
	{ "A1", "3", "3" },
	{ "B1", "=foo(", "#ERROR!" },
	{ "B2", "=window", "#ERROR!" },
	{ "B3", "=eval(\"1\")", "#ERROR!" },
	{ "B4", "=1+", "#ERROR!" },
	{ "B5", "=UNKNOWN(1)", "#ERROR!" },
	{ "B6", "=constructor", "#ERROR!" },
	{ "B7", "text", "text" },
	{ "B8", "#text", "#text" },
	{ "B9", "= A1 * 2 ", "6" },
};
static const CALC_CASE calc_sheet6[] = {
	{ "A1", "-3.7", "-3.7" },
	{ "B1", "=Math.trunc(A1)", "-3" },
	{ "B2", "=Math.sign(A1)", "-1" },
	{ "B3", "=Math.max(1,2,3)", "3" },
	{ "B4", "=new Date(2025,0,15).getDate()", "15" },
	{ "B5", "=Date.UTC(2025,0,15)/86400000", "20103" },
	{ "B6", "=[1,2,3].length", "3" },
	{ "B7", "=\"abc\".toUpperCase()", "ABC" },
	{ "B8", "=(12.5).toFixed(1)", "12.5" },
	{ "B9", "=JSON.stringify([1,\"a\"])", "[1,\"a\"]" },
	{ "B10", "=A1.toString().length", "-3" },
	{ "B11", "=typeof A1", "number" },
};

static const CALC_SHEET calc_sheets[] = {
	{ 1, 19, calc_sheet0 },
	{ 1, 12, calc_sheet1 },
	{ 1, 18, calc_sheet2 },
	{ 1, 17, calc_sheet3 },
	{ 1, 12, calc_sheet4 },
	{ 1, 10, calc_sheet5 },
	{ 0, 12, calc_sheet6 },
};

static const CALC_OLD calc_old[] = {
	{ "=1+2", "=1+2" },
	{ "3=1+2", "=1+2" },
	{ "-1.5=A1*2", "=A1*2" },
	{ "\357\274\235\357\274\2731\357\274\2142\357\274\275\357\274\213\357\274\2733\357\274\2144\357\274\275", "=B1+D3" },
	{ "=[2,3]*2", "=C2*2" },
	{ "\357\274\210\346\240\252\357\274\211", "(\346\240\252)" },
	{ "\"quoted\"", "\"quoted\"" },
	{ "\357\274\235\357\274\241\357\274\221", "=\357\274\241\357\274\221" },
	{ "\357\274\221\357\274\222\357\274\223", "\357\274\221\357\274\222\357\274\223" },
	{ "a\343\200\200b", "a b" },
	{ "=SUM(A1:A3)", "=SUM(A1:A3)" },
};

static const CALC_ADJ calc_adj[] = {
	{ "=A1+B2", 1, 0, "=B1+C2" },
	{ "=A1+B2", 0, 3, "=A4+B5" },
	{ "=$A$1+A1", 2, 2, "=$A$1+C3" },
	{ "=SUM(A1:A3)", 1, 1, "=SUM(B2:B4)" },
	{ "=A1", -1, 0, "=#REF!" },
	{ "=Z1", 1, 0, "=#REF!" },
	{ "=LOG10(A1)", 0, 1, "=#REF!(A2)" },
	{ "=A$1+$B2", 1, 1, "=B$1+$B3" },
	{ "text", 1, 1, "text" },
	{ "=A100", 0, 1, "=#REF!" },
	{ "=\"B2\"&C3", 1, 0, "=\"C2\"&D3" },
};

typedef struct { const char *cell, *set; } CALC_CELLSET;
typedef struct { const char *range, *kind; int x, y, w, h; const char *xml; } CALC_CHART;
typedef struct { const char *in, *out; } CALC_RECORD;

static const CALC_CELLSET calc_chart_cells[] = {
	{ "E7", "A" },
	{ "F7", "1" },
	{ "G7", "2" },
	{ "E8", "B" },
	{ "F8", "4" },
	{ "G8", "3" },
	{ "E9", "C" },
	{ "F9", "2" },
	{ "G9", "=F9/2" },
	{ "E10", "D&<" },
	{ "F10", "-3" },
	{ "G10", "5" },
	{ "E11", "" },
	{ "F11", "x" },
	{ "G11", "2.5" },
	{ "H7", "7" },
	{ "H8", "1" },
	{ "H9", "0" },
	{ "H10", "2" },
	{ "H11", "3" },
};

static const CALC_CHART calc_charts[] = {
	{ "E7:G11", "clustered-bar", 574, 395, 200, 200,
	  "<group left=\"574\" top=\"395\" right=\"774\" bottom=\"595\">\012<calc chartType=\"clustered-bar\" dataRange=\"E7:G11\">\012<figure>\012<figView top=\"0\" left=\"0\" right=\"200\" bottom=\"200\"/>\012<figDraw top=\"0\" left=\"0\" right=\"200\" bottom=\"200\"/>\012<figScale hunit=\"-72\" vunit=\"-72\"/>\012<patterns>\012<pattern id=\"128\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ffffff\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"129\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#51aefb\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"130\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ff7f50\" bgcolor=\"transparent\" masks=\"7\" />\012</patterns>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"1\" l_pat=\"0\" f_pat=\"128\" angle=\"0\" left=\"0\" top=\"0\" right=\"200\" bottom=\"200\" zIndex=\"0\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"33\" top=\"142\" right=\"44\" bottom=\"170\" zIndex=\"100\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"44\" top=\"114\" right=\"55\" bottom=\"170\" zIndex=\"99\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"61\" top=\"58\" right=\"72\" bottom=\"170\" zIndex=\"98\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"72\" top=\"86\" right=\"83\" bottom=\"170\" zIndex=\"97\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"89\" top=\"114\" right=\"100\" bottom=\"170\" zIndex=\"96\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"100\" top=\"142\" right=\"111\" bottom=\"170\" zIndex=\"95\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"117\" top=\"254\" right=\"128\" bottom=\"170\" zIndex=\"94\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"128\" top=\"30\" right=\"139\" bottom=\"170\" zIndex=\"93\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"145\" top=\"170\" right=\"156\" bottom=\"170\" zIndex=\"92\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"156\" top=\"100\" right=\"167\" bottom=\"170\" zIndex=\"91\"/>\012<document>\012<docView viewleft=\"30\" viewtop=\"175\" viewright=\"58\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"30\" drawtop=\"175\" drawright=\"58\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"90\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012A\012</document>\012<document>\012<docView viewleft=\"58\" viewtop=\"175\" viewright=\"86\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"58\" drawtop=\"175\" drawright=\"86\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"89\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012B\012</document>\012<document>\012<docView viewleft=\"86\" viewtop=\"175\" viewright=\"114\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"86\" drawtop=\"175\" drawright=\"114\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"88\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012C\012</document>\012<document>\012<docView viewleft=\"114\" viewtop=\"175\" viewright=\"142\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"114\" drawtop=\"175\" drawright=\"142\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"87\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012D&amp;&lt;\012</document>\012<document>\012<docView viewleft=\"142\" viewtop=\"175\" viewright=\"170\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"142\" drawtop=\"175\" drawright=\"170\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"86\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012\012</document>\012</figure>\012</calc>\012</group>\012" },
	{ "E7:G11", "stacked-bar", 300, 40, 260, 180,
	  "<group left=\"300\" top=\"40\" right=\"560\" bottom=\"220\">\012<calc chartType=\"stacked-bar\" dataRange=\"E7:G11\">\012<figure>\012<figView top=\"0\" left=\"0\" right=\"260\" bottom=\"180\"/>\012<figDraw top=\"0\" left=\"0\" right=\"260\" bottom=\"180\"/>\012<figScale hunit=\"-72\" vunit=\"-72\"/>\012<patterns>\012<pattern id=\"128\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ffffff\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"129\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#51aefb\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"130\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ff7f50\" bgcolor=\"transparent\" masks=\"7\" />\012</patterns>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"1\" l_pat=\"0\" f_pat=\"128\" angle=\"0\" left=\"0\" top=\"0\" right=\"260\" bottom=\"180\" zIndex=\"0\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"38\" top=\"133\" right=\"62\" bottom=\"150\" zIndex=\"100\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"38\" top=\"99\" right=\"62\" bottom=\"133\" zIndex=\"99\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"78\" top=\"81\" right=\"102\" bottom=\"150\" zIndex=\"98\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"78\" top=\"30\" right=\"102\" bottom=\"81\" zIndex=\"97\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"118\" top=\"116\" right=\"142\" bottom=\"150\" zIndex=\"96\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"118\" top=\"99\" right=\"142\" bottom=\"116\" zIndex=\"95\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"158\" top=\"201\" right=\"182\" bottom=\"150\" zIndex=\"94\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"158\" top=\"116\" right=\"182\" bottom=\"201\" zIndex=\"93\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"198\" top=\"150\" right=\"222\" bottom=\"150\" zIndex=\"92\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"198\" top=\"107\" right=\"222\" bottom=\"150\" zIndex=\"91\"/>\012<document>\012<docView viewleft=\"30\" viewtop=\"155\" viewright=\"70\" viewbottom=\"170\"/>\015\012<docDraw drawleft=\"30\" drawtop=\"155\" drawright=\"70\" drawbottom=\"170\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"90\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012A\012</document>\012<document>\012<docView viewleft=\"70\" viewtop=\"155\" viewright=\"110\" viewbottom=\"170\"/>\015\012<docDraw drawleft=\"70\" drawtop=\"155\" drawright=\"110\" drawbottom=\"170\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"89\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012B\012</document>\012<document>\012<docView viewleft=\"110\" viewtop=\"155\" viewright=\"150\" viewbottom=\"170\"/>\015\012<docDraw drawleft=\"110\" drawtop=\"155\" drawright=\"150\" drawbottom=\"170\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"88\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012C\012</document>\012<document>\012<docView viewleft=\"150\" viewtop=\"155\" viewright=\"190\" viewbottom=\"170\"/>\015\012<docDraw drawleft=\"150\" drawtop=\"155\" drawright=\"190\" drawbottom=\"170\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"87\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012D&amp;&lt;\012</document>\012<document>\012<docView viewleft=\"190\" viewtop=\"155\" viewright=\"230\" viewbottom=\"170\"/>\015\012<docDraw drawleft=\"190\" drawtop=\"155\" drawright=\"230\" drawbottom=\"170\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"86\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012\012</document>\012</figure>\012</calc>\012</group>\012" },
	{ "E7:H11", "line", 10, 10, 200, 200,
	  "<group left=\"10\" top=\"10\" right=\"210\" bottom=\"210\">\012<calc chartType=\"line\" dataRange=\"E7:H11\">\012<figure>\012<figView top=\"0\" left=\"0\" right=\"200\" bottom=\"200\"/>\012<figDraw top=\"0\" left=\"0\" right=\"200\" bottom=\"200\"/>\012<figScale hunit=\"-72\" vunit=\"-72\"/>\012<patterns>\012<pattern id=\"128\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ffffff\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"129\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#51aefb\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"130\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ff7f50\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"131\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#90ee90\" bgcolor=\"transparent\" masks=\"7\" />\012</patterns>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"1\" l_pat=\"0\" f_pat=\"128\" angle=\"0\" left=\"0\" top=\"0\" right=\"200\" bottom=\"200\" zIndex=\"0\"/>\012<polyline lineType=\"0\" lineWidth=\"2\" l_pat=\"129\" f_pat=\"0\" points=\"30,150 65,90 100,130 135,230 170,170\" zIndex=\"100\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"26\" frameTop=\"146\" frameRight=\"34\" frameBottom=\"154\" zIndex=\"99\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"61\" frameTop=\"86\" frameRight=\"69\" frameBottom=\"94\" zIndex=\"98\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"96\" frameTop=\"126\" frameRight=\"104\" frameBottom=\"134\" zIndex=\"97\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"131\" frameTop=\"226\" frameRight=\"139\" frameBottom=\"234\" zIndex=\"96\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"166\" frameTop=\"166\" frameRight=\"174\" frameBottom=\"174\" zIndex=\"95\"/>\012<polyline lineType=\"0\" lineWidth=\"2\" l_pat=\"130\" f_pat=\"0\" points=\"30,130 65,110 100,150 135,70 170,120\" zIndex=\"94\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"26\" frameTop=\"126\" frameRight=\"34\" frameBottom=\"134\" zIndex=\"93\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"61\" frameTop=\"106\" frameRight=\"69\" frameBottom=\"114\" zIndex=\"92\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"96\" frameTop=\"146\" frameRight=\"104\" frameBottom=\"154\" zIndex=\"91\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"131\" frameTop=\"66\" frameRight=\"139\" frameBottom=\"74\" zIndex=\"90\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"166\" frameTop=\"116\" frameRight=\"174\" frameBottom=\"124\" zIndex=\"89\"/>\012<polyline lineType=\"0\" lineWidth=\"2\" l_pat=\"131\" f_pat=\"0\" points=\"30,30 65,150 100,170 135,130 170,110\" zIndex=\"88\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"26\" frameTop=\"26\" frameRight=\"34\" frameBottom=\"34\" zIndex=\"87\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"61\" frameTop=\"146\" frameRight=\"69\" frameBottom=\"154\" zIndex=\"86\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"96\" frameTop=\"166\" frameRight=\"104\" frameBottom=\"174\" zIndex=\"85\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"131\" frameTop=\"126\" frameRight=\"139\" frameBottom=\"134\" zIndex=\"84\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"166\" frameTop=\"106\" frameRight=\"174\" frameBottom=\"114\" zIndex=\"83\"/>\012<document>\012<docView viewleft=\"13\" viewtop=\"175\" viewright=\"48\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"13\" drawtop=\"175\" drawright=\"48\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"82\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012A\012</document>\012<document>\012<docView viewleft=\"48\" viewtop=\"175\" viewright=\"83\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"48\" drawtop=\"175\" drawright=\"83\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"81\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012B\012</document>\012<document>\012<docView viewleft=\"83\" viewtop=\"175\" viewright=\"118\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"83\" drawtop=\"175\" drawright=\"118\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"80\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012C\012</document>\012<document>\012<docView viewleft=\"118\" viewtop=\"175\" viewright=\"153\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"118\" drawtop=\"175\" drawright=\"153\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"79\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012D&amp;&lt;\012</document>\012<document>\012<docView viewleft=\"153\" viewtop=\"175\" viewright=\"188\" viewbottom=\"190\"/>\015\012<docDraw drawleft=\"153\" drawtop=\"175\" drawright=\"188\" drawbottom=\"190\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"78\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012\012</document>\012</figure>\012</calc>\012</group>\012" },
	{ "E7:H11", "stacked-line", 0, 0, 333, 211,
	  "<group left=\"0\" top=\"0\" right=\"333\" bottom=\"211\">\012<calc chartType=\"stacked-line\" dataRange=\"E7:H11\">\012<figure>\012<figView top=\"0\" left=\"0\" right=\"333\" bottom=\"211\"/>\012<figDraw top=\"0\" left=\"0\" right=\"333\" bottom=\"211\"/>\012<figScale hunit=\"-72\" vunit=\"-72\"/>\012<patterns>\012<pattern id=\"128\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ffffff\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"129\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#51aefb\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"130\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ff7f50\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"131\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#90ee90\" bgcolor=\"transparent\" masks=\"7\" />\012</patterns>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"1\" l_pat=\"0\" f_pat=\"128\" angle=\"0\" left=\"0\" top=\"0\" right=\"333\" bottom=\"211\" zIndex=\"0\"/>\012<polyline lineType=\"0\" lineWidth=\"2\" l_pat=\"129\" f_pat=\"0\" points=\"30,166 98,121 167,151 235,226 303,181\" zIndex=\"100\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"26\" frameTop=\"162\" frameRight=\"34\" frameBottom=\"170\" zIndex=\"99\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"94\" frameTop=\"117\" frameRight=\"102\" frameBottom=\"125\" zIndex=\"98\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"163\" frameTop=\"147\" frameRight=\"171\" frameBottom=\"155\" zIndex=\"97\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"231\" frameTop=\"222\" frameRight=\"239\" frameBottom=\"230\" zIndex=\"96\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"299\" frameTop=\"177\" frameRight=\"307\" frameBottom=\"185\" zIndex=\"95\"/>\012<polyline lineType=\"0\" lineWidth=\"2\" l_pat=\"130\" f_pat=\"0\" points=\"30,136 98,75 167,136 235,151 303,143\" zIndex=\"94\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"26\" frameTop=\"132\" frameRight=\"34\" frameBottom=\"140\" zIndex=\"93\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"94\" frameTop=\"71\" frameRight=\"102\" frameBottom=\"79\" zIndex=\"92\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"163\" frameTop=\"132\" frameRight=\"171\" frameBottom=\"140\" zIndex=\"91\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"231\" frameTop=\"147\" frameRight=\"239\" frameBottom=\"155\" zIndex=\"90\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" frameLeft=\"299\" frameTop=\"139\" frameRight=\"307\" frameBottom=\"147\" zIndex=\"89\"/>\012<polyline lineType=\"0\" lineWidth=\"2\" l_pat=\"131\" f_pat=\"0\" points=\"30,30 98,60 167,136 235,121 303,98\" zIndex=\"88\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"26\" frameTop=\"26\" frameRight=\"34\" frameBottom=\"34\" zIndex=\"87\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"94\" frameTop=\"56\" frameRight=\"102\" frameBottom=\"64\" zIndex=\"86\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"163\" frameTop=\"132\" frameRight=\"171\" frameBottom=\"140\" zIndex=\"85\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"231\" frameTop=\"117\" frameRight=\"239\" frameBottom=\"125\" zIndex=\"84\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"131\" angle=\"0\" frameLeft=\"299\" frameTop=\"94\" frameRight=\"307\" frameBottom=\"102\" zIndex=\"83\"/>\012<document>\012<docView viewleft=\"-4\" viewtop=\"186\" viewright=\"64\" viewbottom=\"201\"/>\015\012<docDraw drawleft=\"-4\" drawtop=\"186\" drawright=\"64\" drawbottom=\"201\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"82\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012A\012</document>\012<document>\012<docView viewleft=\"64\" viewtop=\"186\" viewright=\"132\" viewbottom=\"201\"/>\015\012<docDraw drawleft=\"64\" drawtop=\"186\" drawright=\"132\" drawbottom=\"201\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"81\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012B\012</document>\012<document>\012<docView viewleft=\"132\" viewtop=\"186\" viewright=\"200\" viewbottom=\"201\"/>\015\012<docDraw drawleft=\"132\" drawtop=\"186\" drawright=\"200\" drawbottom=\"201\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"80\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012C\012</document>\012<document>\012<docView viewleft=\"201\" viewtop=\"186\" viewright=\"269\" viewbottom=\"201\"/>\015\012<docDraw drawleft=\"201\" drawtop=\"186\" drawright=\"269\" drawbottom=\"201\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"79\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012D&amp;&lt;\012</document>\012<document>\012<docView viewleft=\"269\" viewtop=\"186\" viewright=\"337\" viewbottom=\"201\"/>\015\012<docDraw drawleft=\"269\" drawtop=\"186\" drawright=\"337\" drawbottom=\"201\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"78\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012\012</document>\012</figure>\012</calc>\012</group>\012" },
	{ "E7:F7", "line", 5, 5, 120, 90,
	  "<group left=\"5\" top=\"5\" right=\"125\" bottom=\"95\">\012<calc chartType=\"line\" dataRange=\"E7:F7\">\012<figure>\012<figView top=\"0\" left=\"0\" right=\"120\" bottom=\"90\"/>\012<figDraw top=\"0\" left=\"0\" right=\"120\" bottom=\"90\"/>\012<figScale hunit=\"-72\" vunit=\"-72\"/>\012<patterns>\012<pattern id=\"128\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ffffff\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"129\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#51aefb\" bgcolor=\"transparent\" masks=\"7\" />\012</patterns>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"1\" l_pat=\"0\" f_pat=\"128\" angle=\"0\" left=\"0\" top=\"0\" right=\"120\" bottom=\"90\" zIndex=\"0\"/>\012<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" frameLeft=\"56\" frameTop=\"26\" frameRight=\"64\" frameBottom=\"34\" zIndex=\"100\"/>\012<document>\012<docView viewleft=\"30\" viewtop=\"65\" viewright=\"90\" viewbottom=\"80\"/>\015\012<docDraw drawleft=\"30\" drawtop=\"65\" drawright=\"90\" drawbottom=\"80\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"99\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012A\012</document>\012</figure>\012</calc>\012</group>\012" },
	{ "E7:G7", "clustered-bar", 5, 5, 100, 100,
	  "<group left=\"5\" top=\"5\" right=\"105\" bottom=\"105\">\012<calc chartType=\"clustered-bar\" dataRange=\"E7:G7\">\012<figure>\012<figView top=\"0\" left=\"0\" right=\"100\" bottom=\"100\"/>\012<figDraw top=\"0\" left=\"0\" right=\"100\" bottom=\"100\"/>\012<figScale hunit=\"-72\" vunit=\"-72\"/>\012<patterns>\012<pattern id=\"128\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ffffff\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"129\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#51aefb\" bgcolor=\"transparent\" masks=\"7\" />\012<pattern id=\"130\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"#ff7f50\" bgcolor=\"transparent\" masks=\"7\" />\012</patterns>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"1\" l_pat=\"0\" f_pat=\"128\" angle=\"0\" left=\"0\" top=\"0\" right=\"100\" bottom=\"100\" zIndex=\"0\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"129\" angle=\"0\" left=\"34\" top=\"50\" right=\"50\" bottom=\"70\" zIndex=\"100\"/>\012<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"130\" angle=\"0\" left=\"50\" top=\"30\" right=\"66\" bottom=\"70\" zIndex=\"99\"/>\012<document>\012<docView viewleft=\"30\" viewtop=\"75\" viewright=\"70\" viewbottom=\"90\"/>\015\012<docDraw drawleft=\"30\" drawtop=\"75\" drawright=\"70\" drawbottom=\"90\"/>\015\012<docScale hunit=\"-72\" vunit=\"-72\"/>\015\012<text lang=\"0\" bpat=\"0\" zIndex=\"98\"/>\015\012<font size=\"10\"/>\012<font face=\"sans-serif\"/>\012<font color=\"#000000\"/>\012<text align=\"center\"/>\012A\012</document>\012</figure>\012</calc>\012</group>\012" },
};

static const CALC_RECORD calc_records[] = {
	{ "<tad version=\"1.0\" filename=\"\350\241\250\" encoding=\"UTF-8\">\012<document>\012<calcSize cell=\"3\" height=\"40\"/>\012<calcSize cell=\"B\" width=\"137\"/>\012<calcSize cell=\"D\" width=\"80\"/>\012<calcMerge range=\"C3:D4\"/>\012<p>\012<calcPos cell=\"A1\"/>\350\246\213\345\207\272\343\201\227\011<calcPos cell=\"B1\"/><calcCell bgCol=\"#ffff00\" borderTop=\"1\" borderTopType=\"dash\" borderTopWidth=\"2\" borderTopColor=\"#ff0000\"/><text align=\"center\"/><font color=\"#0000ff\"/><font size=\"16\"/><bold><italic>\345\244\252\343\201\204</italic></bold>\011<calcPos cell=\"C1\"/>=A2+B2</p>\012<p><calcPos cell=\"A2\"/>1\011<calcPos cell=\"B2\"/>\357\274\222\011<calcPos cell=\"C2\"/>\357\274\235\357\274\2732\357\274\2141\357\274\275\357\274\2123\011<calcPos cell=\"D2\"/><cell valign=\"top\"/><underline><strikethrough>a&amp;b&lt;c</strikethrough></underline></p>\012<p><calcPos cell=\"A3\"/>\"\345\274\225\347\224\250\"\011<calcPos cell=\"B3\"/>  \347\251\272\347\231\275  \011<calcPos cell=\"C3\"/>\347\265\220\345\220\210\011<calcPos cell=\"D3\"/><calcCell borderLeft=\"1\" borderLeftType=\"dot\" borderLeftWidth=\"1\" borderLeftColor=\"#000000\" borderRight=\"1\" borderRightType=\"double\" borderRightWidth=\"3\" borderRightColor=\"#00ff00\"/></p>\012<p><calcPos cell=\"A4\"/><font face=\"MS Mincho\"/><superscript>\344\270\212</superscript>\011<calcPos cell=\"B4\"/><subscript>\344\270\213</subscript><mesh>\347\266\262</mesh><invert>\345\217\215</invert>\011<calcPos cell=\"C4\"/><text align=\"left\"/><font color=\"#000000\"/><font size=\"12\"/>\346\250\231\346\272\226\011<calcPos cell=\"D4\"/>=SUM(A2:B2)*2</p>\012</document>\012</tad>\012",
	  "<tad version=\"1.0\" filename=\"\350\241\250\" encoding=\"UTF-8\">\012<document>\012<calcSize cell=\"3\" height=\"40\"/>\012<calcSize cell=\"B\" width=\"137\"/>\012<calcMerge range=\"C3:D4\"/>\012<p>\012<calcPos cell=\"A1\"/>\350\246\213\345\207\272\343\201\227\011<calcPos cell=\"B1\"/><calcCell bgCol=\"#ffff00\" borderTop=\"1\" borderTopType=\"dash\" borderTopWidth=\"2\" borderTopColor=\"#ff0000\"/><text align=\"center\"/><font color=\"#0000ff\"/><font size=\"16\"/><italic><bold>\345\244\252\343\201\204</bold></italic>\011<calcPos cell=\"C1\"/>=A2+B2\011<calcPos cell=\"D1\"/></p>\012<p><calcPos cell=\"A2\"/>1\011<calcPos cell=\"B2\"/>\357\274\222\011<calcPos cell=\"C2\"/>=A2*3\011<calcPos cell=\"D2\"/><cell valign=\"top\"/><strikethrough><underline>a&amp;b&lt;c</underline></strikethrough></p>\012<p><calcPos cell=\"A3\"/>\345\274\225\347\224\250\011<calcPos cell=\"B3\"/>\347\251\272\347\231\275\011<calcPos cell=\"C3\"/>\347\265\220\345\220\210\011<calcPos cell=\"D3\"/><calcCell borderLeft=\"1\" borderLeftType=\"dot\" borderLeftWidth=\"1\" borderLeftColor=\"#000000\" borderRight=\"1\" borderRightType=\"line\" borderRightWidth=\"3\" borderRightColor=\"#00ff00\"/></p>\012<p><calcPos cell=\"A4\"/><font face=\"MS Mincho\"/><superscript>\344\270\212</superscript>\011<calcPos cell=\"B4\"/><subscript>\344\270\213\347\266\262\345\217\215</subscript>\011<calcPos cell=\"C4\"/>\346\250\231\346\272\226\011<calcPos cell=\"D4\"/>=SUM(A2:B2)*2</p>\012</document>\012</tad>\012" },
	{ "<tad version=\"1.0\" filename=\"\346\227\247\" encoding=\"UTF-8\">\012<document>\012<p><calcPos col=\"1\" row=\"2\"/>12\011<calcPos col=\"2\" row=\"1\"/>3=\357\274\2731\357\274\2142\357\274\275+1\011<calcPos col=\"2\" row=\"2\"/>abc</p>\012</document>\012</tad>\012",
	  "<tad version=\"1.0\" filename=\"\346\227\247\" encoding=\"UTF-8\">\012<document>\012<p>\012<calcPos cell=\"A1\"/>\011<calcPos cell=\"B1\"/>12</p>\012<p><calcPos cell=\"A2\"/>=B1+1\011<calcPos cell=\"B2\"/>abc</p>\012</document>\012</tad>\012" },
	{ "<tad version=\"1.0\" filename=\"\345\237\272\346\234\254\350\241\250\350\250\210\347\256\227\" encoding=\"UTF-8\">\012<document>\012<p>\012<calcPos cell=\"A1\"/>\011<calcPos cell=\"B1\"/>\011<calcPos cell=\"C1\"/>\011<calcPos cell=\"D1\"/></p>\012<calcPos cell=\"A2\"/>\011<calcPos cell=\"B2\"/>\011<calcPos cell=\"C2\"/>\011<calcPos cell=\"D2\"/></p>\012<p>\012</document>\012</tad>",
	  "<tad version=\"1.0\" filename=\"\345\237\272\346\234\254\350\241\250\350\250\210\347\256\227\" encoding=\"UTF-8\">\012<document>\012<p>\012<calcPos cell=\"A1\"/></p>\012</document>\012</tad>\012" },
};
