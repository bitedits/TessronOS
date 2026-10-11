/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	calc_cases.js -- the answers 基本表計算 must give (design 11.13)
 *
 *	node tools/calc_cases.js <editor.js of the plugin> [out.h]
 *
 *	Takes the formula methods of the plugin's editor out of its source
 *	as they are (evaluateFormula and the methods it calls, the loading
 *	conversion and the moving of references), runs the sheets below
 *	through them with formula.min.js and jstat.min.js, and writes what
 *	each cell shows as a C table (application/calc/calc_cases.h). The
 *	same sheets run through application/calc/js/calc_engine.js must give
 *	the same words; the program's self test (calc_test.c) and the
 *	kernel's (tests/ktest/ktest_calc.c) compare against the table.
 */

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const TOP = path.resolve(__dirname, '..');
const JS = path.join(TOP, 'application', 'calc', 'js');
const editorPath = process.argv[2];
const outPath = process.argv[3] || path.join(TOP, 'application', 'calc', 'calc_cases.h');

if (!editorPath) {
	console.error('usage: node calc_cases.js <editor.js> [out.h]');
	process.exit(2);
}

/* One method of the class, its text from "    name(" to the "    }" that closes it */
function method(src, name) {
	const lines = src.split('\n');
	const start = lines.findIndex(l => new RegExp('^    (async )?' + name + '\\(').test(l));
	if (start < 0) throw new Error('no method ' + name);
	let end = start + 1;
	while (end < lines.length && !/^    }\s*$/.test(lines[end])) end++;
	return lines.slice(start, end + 1).join('\n');
}

function context() {
	const ctx = { console };
	vm.createContext(ctx);
	vm.runInContext(fs.readFileSync(path.join(JS, 'jstat.min.js'), 'utf8'), ctx);
	vm.runInContext(fs.readFileSync(path.join(JS, 'formula.min.js'), 'utf8'), ctx);
	return ctx;
}

/* The plugin's own methods on a sheet of its own */
function plugin() {
	const src = fs.readFileSync(editorPath, 'utf8');
	const names = ['getCellKey', 'colToLetter', 'letterToCol', 'evaluateFormula', 'getCellValueWithRecalc',
		       'recalculateDependentCells', 'setCellValue', 'resetReplacedVirtualObjectView',
		       'convertFullWidthToHalfWidth', 'convertOldFormulaFormat', 'adjustFormulaReferences'];
	const ctx = context();
	ctx.window = { formulajs: ctx.formulajs };
	ctx.logger = { info() {}, debug() {}, warn() {}, error() {} };
	const body = names.map(n => method(src, n)).join('\n');
	vm.runInContext('class Sheet {\n constructor() { this.cells = new Map(); this.currentMaxCol = 26; ' +
			'this.currentMaxRow = 100; }\n renderCell() {}\n updateChartsForCell() {}\n' + body + '\n}\n' +
			'globalThis.Sheet = Sheet;', ctx);
	const sheet = new ctx.Sheet();
	return {
		sheet,
		set(c, r, v) { sheet.setCellValue(c, r, v); },
		shown(c, r) {
			const d = sheet.cells.get(c + ',' + r);
			return d ? String(d.displayValue || d.value || '') : '';
		},
		old(v) { return sheet.convertOldFormulaFormat(v); },
		adjust(f, dc, dr) { return sheet.adjustFormulaReferences(f, dc, dr); }
	};
}

/*
 * The plugin's own reading of a record and writing of it again, its
 * screen and its messages left out. Links, images, the paper and charts
 * are not in the records given it (the program keeps a link and an
 * image as they were read, and its charts are tested by themselves).
 */
const ESCAPE = String.raw`escapeXml(t) { if (typeof t !== "string") return ""; return t.replace(/\\u200B/g, "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/\'/g, "&apos;"); }`;

async function plugin_record(xtad, name) {
	const src = fs.readFileSync(editorPath, 'utf8');
	const names = ['getCellKey', 'colToLetter', 'letterToCol', 'evaluateFormula', 'getCellValueWithRecalc',
		       'recalculateDependentCells', 'setCellValue', 'setCellStyle', 'resetReplacedVirtualObjectView',
		       'convertFullWidthToHalfWidth', 'convertOldFormulaFormat', 'loadXtadData', 'parseCalcCellTag',
		       'parseCellValueWithFormatting', 'generateXtadData', 'generateCalcCellTag',
		       'generateCellValueWithFormatting'];
	const ctx = context();
	ctx.window = { formulajs: ctx.formulajs };
	ctx.logger = { info() {}, debug() {}, warn() {}, error() {} };
	ctx.DEFAULT_BGCOL = '#ffffff';
	ctx.DEFAULT_CHCOL = '#000000';
	ctx.DEFAULT_FRCOL = '#000000';
	const body = names.map(n => method(src, n)).join('\n');
	vm.runInContext('class Sheet {\n constructor() { this.cells = new Map(); this.currentMaxCol = 26; ' +
			'this.currentMaxRow = 100; this.defaultRows = 100; this.defaultCols = 26; this.cellWidth = 80; ' +
			'this.cellHeight = 22; this.columnWidths = new Map(); this.rowHeights = new Map(); ' +
			'this.mergedCells = new Map(); this.charts = []; this.paperSize = null; this.paperMargin = null; ' +
			'this.maxRowLimit = 1000000; this.maxColLimit = 16384; }\n' +
			' renderCell() {}\n updateChartsForCell() {}\n applyColumnWidth() {}\n applyRowHeight() {}\n' +
			' async preloadVirtualObjectIcons() {}\n renderMergedCells() {}\n loadChartsFromXtad() {}\n' +
			' async autoOpenVirtualObjects() {}\n' +
			' expandGridTo(r, c) { if (r > this.currentMaxRow) this.currentMaxRow = r; if (c > this.currentMaxCol) this.currentMaxCol = c; }\n' +
			ESCAPE + '\n' + body + '\n}\nglobalThis.Sheet = Sheet;', ctx);
	const sheet = new ctx.Sheet();
	sheet.fileName = name;
	await sheet.loadXtadData(xtad);
	return sheet.generateXtadData();
}

/* The plugin's figure of a chart */
function plugin_chart(sheetCells, range, kind, x, y, w, h) {
	const mixin = fs.readFileSync(path.join(path.dirname(editorPath), 'calc-chart-mixin.js'), 'utf8');
	const base = fs.readFileSync(path.join(path.dirname(editorPath), '..', '..', 'js', 'plugin-base.js'), 'utf8');
	const p = plugin();
	for (const [r, v] of sheetCells) {
		const [c, rr] = ref(r);
		p.set(c, rr, v);
	}
	const ctx = context();
	const names = ['getChartDataFromRange', 'getChartFillPatternId', 'generateChartXtad',
		       'generateClusteredBarChartElements', 'generateStackedBarChartElements',
		       'generateLineChartElements', 'generateStackedLineChartElements', 'parseDataRange'];
	const body = names.map(n => method(mixin, n)).join('\n');
	const more = ['generateDocViewDrawScale', 'generateTextElement'].map(n => method(base, n)).join('\n');
	ctx.findSolidColorPatternId = () => 0;
	vm.runInContext('class Charts {\n constructor() { this.chartColors = ["#51aefb", "#ff7f50", "#90ee90", ' +
			'"#dda0dd", "#f0e68c", "#87ceeb"]; }\n getCellKey(c, r) { return c + "," + r; }\n' +
			' colToLetter(col) { let l = ""; while (col > 0) { col--; l = String.fromCharCode(65 + (col % 26)) + l; ' +
			'col = Math.floor(col / 26); } return l; }\n' +
			' letterToCol(s) { let c = 0; for (const ch of s) c = c * 26 + ch.charCodeAt(0) - 64; return c; }\n' +
			ESCAPE + '\n' + body + '\n' + more + '\n}\nglobalThis.Charts = Charts;', ctx);
	const ch = new ctx.Charts();
	ch.cells = p.sheet.cells;
	const rg = ch.parseDataRange(range);
	return ch.generateChartXtad({ x, y, width: w, height: h, chartType: kind, dataRange: range,
				      data: ch.getChartDataFromRange(rg) });
}

/* The program's engine */
function engine() {
	const ctx = context();
	vm.runInContext(fs.readFileSync(path.join(JS, 'calc_engine.js'), 'utf8') + '\nglobalThis.calc = calc;', ctx);
	return {
		set(c, r, v) { ctx.calc.set(c, r, v); },
		shown(c, r) { return ctx.calc.shown(c, r); }
	};
}

function ref(s) {
	const m = /^([A-Z]+)(\d+)$/.exec(s);
	let col = 0;
	for (const ch of m[1]) col = col * 26 + ch.charCodeAt(0) - 64;
	return [col, parseInt(m[2])];
}

/*
 * The sheets: each a list of cells set in order. 'c' marks a sheet the
 * small evaluator of a program built without the engine (calc_ceval.c)
 * answers too.
 */
const SHEETS = [
	{ c: true, cells: [
		['A1', '10'], ['A2', '20'], ['A3', '30'], ['B1', '=A1+A2'], ['B2', '=A1*A2-A3'],
		['B3', '=(A1+A2)/A3'], ['B4', '=A3/7'], ['B5', '=A1%3'], ['B6', '=2**10'], ['B7', '=-A1+5'],
		['B8', '=0.1+0.2'], ['B9', '=1/3'], ['B10', '=A1/0'], ['B11', '=0/0'], ['B12', '=A1-A1'],
		['B13', '=1e21*10'], ['B14', '=123456789*1000'], ['B15', '=2.5*4'], ['B16', '=7/2'] ] },
	{ c: true, cells: [
		['A1', '10'], ['A2', '20'], ['A3', '30'], ['C1', '=SUM(A1:A3)'], ['C2', '=AVERAGE(A1:A3)'],
		['C3', '=COUNT(A1:B3)'], ['C4', '=MAX(A1:A3)'], ['C5', '=MIN(A1:A3)'], ['C6', '=SUM(A1:A3)*2'],
		['C7', '=MAX(A1:A3,100)'], ['C8', '=SUM(A1,A2,5)'], ['C9', '=AVERAGE(E1:E3)'] ] },
	{ c: true, cells: [
		['A1', '2.345'], ['A2', '-2.5'], ['A3', '3.7'], ['B1', '=ROUND(A1,2)'], ['B2', '=ROUND(A2,0)'],
		['B3', '=ROUND(A3)'], ['B4', '=FLOOR(A3,1)'], ['B5', '=CEIL(3.2,1)'], ['B6', '=ABS(A2)'],
		['B7', '=SQRT(16)'], ['B8', '=POWER(2,0.5)'], ['B9', '=MOD(10,3)'], ['B10', '=MOD(-7,3)'],
		['B11', '=PI()'], ['B12', '=ROUND(PI(),4)'], ['B13', '=SQRT(-1)'], ['B14', '=MOD(5,0)'],
		['B15', '=ROUND(1234.5678,-2)'] ] },
	{ c: true, cells: [
		['A1', '8'], ['A2', 'abc'], ['A3', ''], ['B1', '=IF(A1>5,"big","small")'], ['B2', '=IF(A1<5,1,2)'],
		['B3', '=A1>5'], ['B4', '=A1<5'], ['B5', '=A2'], ['B6', '=A1+A2'], ['B7', '="x"+"y"'],
		['B8', '=A3+1'], ['B9', '=A1==8'], ['B10', '=A1>=8&&A1<=9'], ['B11', '=A1!=8||A1>100'],
		['B12', '=A1>5?"yes":"no"'], ['B13', '=!(A1>5)'], ['B14', '=A2+1'] ] },
	{ c: true, cells: [
		['A1', '1'], ['A2', '=A1+1'], ['A3', '=A2*10'], ['A4', '=A3+A2+A1'], ['A1', '5'],
		['B1', '=B1+1'], ['B2', '=SUM(B1:B3)'], ['B3', '=C1'], ['C1', '7'], ['B4', '=[1,1]+[2,1]'],
		['B5', '=［1，1］*2'], ['B6', '=SUM(B5:B6)'] ] },
	{ c: true, cells: [
		['A1', '3'], ['B1', '=foo('], ['B2', '=window'], ['B3', '=eval("1")'], ['B4', '=1+'],
		['B5', '=UNKNOWN(1)'], ['B6', '=constructor'], ['B7', 'text'], ['B8', '#text'], ['B9', '= A1 * 2 '] ] },
	{ c: false, cells: [
		['A1', '-3.7'], ['B1', '=Math.trunc(A1)'], ['B2', '=Math.sign(A1)'], ['B3', '=Math.max(1,2,3)'],
		['B4', '=new Date(2025,0,15).getDate()'], ['B5', '=Date.UTC(2025,0,15)/86400000'],
		['B6', '=[1,2,3].length'], ['B7', '="abc".toUpperCase()'], ['B8', '=(12.5).toFixed(1)'],
		['B9', '=JSON.stringify([1,"a"])'], ['B10', '=A1.toString().length'], ['B11', '=typeof A1'] ] }
];

/* What a cell holds when read: the conversion of the loading */
const OLD = ['=1+2', '3=1+2', '-1.5=A1*2', '＝［1，2］＋［3，4］', '=[2,3]*2', '（株）', '"quoted"',
	     '＝Ａ１', '１２３', 'a　b', '=SUM(A1:A3)'];

/* References moved by a copy or a fill: [formula, columns, rows] */
const ADJUST = [['=A1+B2', 1, 0], ['=A1+B2', 0, 3], ['=$A$1+A1', 2, 2], ['=SUM(A1:A3)', 1, 1],
		['=A1', -1, 0], ['=Z1', 1, 0], ['=LOG10(A1)', 0, 1], ['=A$1+$B2', 1, 1], ['text', 1, 1],
		['=A100', 0, 1], ['="B2"&C3', 1, 0]];

/* Charts: the cells, the range, the kind, where and how large */
const CHART_CELLS = [['E7', 'A'], ['F7', '1'], ['G7', '2'], ['E8', 'B'], ['F8', '4'], ['G8', '3'],
		     ['E9', 'C'], ['F9', '2'], ['G9', '=F9/2'], ['E10', 'D&<'], ['F10', '-3'], ['G10', '5'],
		     ['E11', ''], ['F11', 'x'], ['G11', '2.5'], ['H7', '7'], ['H8', '1'], ['H9', '0'], ['H10', '2'], ['H11', '3']];
const CHARTS = [['E7:G11', 'clustered-bar', 574, 395, 200, 200], ['E7:G11', 'stacked-bar', 300, 40, 260, 180],
		['E7:H11', 'line', 10, 10, 200, 200], ['E7:H11', 'stacked-line', 0, 0, 333, 211],
		['E7:F7', 'line', 5, 5, 120, 90], ['E7:G7', 'clustered-bar', 5, 5, 100, 100]];

/* Records read and written again */
const RECORDS = [
	'<tad version="1.0" filename="表" encoding="UTF-8">\n<document>\n<calcSize cell="3" height="40"/>\n' +
	'<calcSize cell="B" width="137"/>\n<calcSize cell="D" width="80"/>\n<calcMerge range="C3:D4"/>\n<p>\n' +
	'<calcPos cell="A1"/>見出し\t<calcPos cell="B1"/><calcCell bgCol="#ffff00" borderTop="1" borderTopType="dash" ' +
	'borderTopWidth="2" borderTopColor="#ff0000"/><text align="center"/><font color="#0000ff"/><font size="16"/>' +
	'<bold><italic>太い</italic></bold>\t<calcPos cell="C1"/>=A2+B2</p>\n' +
	'<p><calcPos cell="A2"/>1\t<calcPos cell="B2"/>２\t<calcPos cell="C2"/>＝［2，1］＊3\t<calcPos cell="D2"/>' +
	'<cell valign="top"/><underline><strikethrough>a&amp;b&lt;c</strikethrough></underline></p>\n' +
	'<p><calcPos cell="A3"/>"引用"\t<calcPos cell="B3"/>  空白  \t<calcPos cell="C3"/>結合\t<calcPos cell="D3"/>' +
	'<calcCell borderLeft="1" borderLeftType="dot" borderLeftWidth="1" borderLeftColor="#000000" borderRight="1" ' +
	'borderRightType="double" borderRightWidth="3" borderRightColor="#00ff00"/></p>\n' +
	'<p><calcPos cell="A4"/><font face="MS Mincho"/><superscript>上</superscript>\t<calcPos cell="B4"/>' +
	'<subscript>下</subscript><mesh>網</mesh><invert>反</invert>\t<calcPos cell="C4"/><text align="left"/>' +
	'<font color="#000000"/><font size="12"/>標準\t<calcPos cell="D4"/>=SUM(A2:B2)*2</p>\n</document>\n</tad>\n',
	'<tad version="1.0" filename="旧" encoding="UTF-8">\n<document>\n<p><calcPos col="1" row="2"/>12\t' +
	'<calcPos col="2" row="1"/>3=［1，2］+1\t<calcPos col="2" row="2"/>abc</p>\n</document>\n</tad>\n',
	'<tad version="1.0" filename="基本表計算" encoding="UTF-8">\n<document>\n<p>\n<calcPos cell="A1"/>\t' +
	'<calcPos cell="B1"/>\t<calcPos cell="C1"/>\t<calcPos cell="D1"/></p>\n<calcPos cell="A2"/>\t<calcPos cell="B2"/>' +
	'\t<calcPos cell="C2"/>\t<calcPos cell="D2"/></p>\n<p>\n</document>\n</tad>'
];

function cstr(s) {
	let o = '"';
	for (const b of Buffer.from(String(s), 'utf8')) {
		if (b === 0x22 || b === 0x5C) o += '\\' + String.fromCharCode(b);
		else if (b < 0x20 || b >= 0x7F) o += '\\' + b.toString(8).padStart(3, '0');
		else o += String.fromCharCode(b);
	}
	return o + '"';
}

const lines = [];
let bad = 0;
lines.push('/*');
lines.push(' * calc_cases.h -- made by tools/calc_cases.js; not to be edited.');
lines.push(' * What each cell of the sheets shows, as the formula methods of the');
lines.push(' * editor and formula.min.js work it out.');
lines.push(' */');
lines.push('');
lines.push('typedef struct { const char *cell, *set, *shows; } CALC_CASE;');
lines.push('typedef struct { int c; int n; const CALC_CASE *cells; } CALC_SHEET;');
lines.push('typedef struct { const char *in, *out; } CALC_OLD;');
lines.push('typedef struct { const char *in; int dc, dr; const char *out; } CALC_ADJ;');
lines.push('');
SHEETS.forEach((sh, i) => {
	const p = plugin(), e = engine();
	for (const [r, v] of sh.cells) {
		const [c, rr] = ref(r);
		p.set(c, rr, v);
		e.set(c, rr, v);
	}
	lines.push('static const CALC_CASE calc_sheet' + i + '[] = {');
	for (const [r, v] of sh.cells) {
		const [c, rr] = ref(r);
		const want = p.shown(c, rr), got = e.shown(c, rr);
		if (want !== got) {
			console.error('sheet ' + i + ' ' + r + ': the plugin shows ' + JSON.stringify(want) +
				      ', the engine ' + JSON.stringify(got));
			bad++;
		}
		lines.push('\t{ ' + cstr(r) + ', ' + cstr(v) + ', ' + cstr(want) + ' },');
	}
	lines.push('};');
});
lines.push('');
lines.push('static const CALC_SHEET calc_sheets[] = {');
SHEETS.forEach((sh, i) => {
	lines.push('\t{ ' + (sh.c ? 1 : 0) + ', ' + sh.cells.length + ', calc_sheet' + i + ' },');
});
lines.push('};');
lines.push('');
{
	const p = plugin();
	lines.push('static const CALC_OLD calc_old[] = {');
	for (const v of OLD) lines.push('\t{ ' + cstr(v) + ', ' + cstr(p.old(v)) + ' },');
	lines.push('};');
	lines.push('');
	lines.push('static const CALC_ADJ calc_adj[] = {');
	for (const [f, dc, dr] of ADJUST) {
		lines.push('\t{ ' + cstr(f) + ', ' + dc + ', ' + dr + ', ' + cstr(p.adjust(f, dc, dr)) + ' },');
	}
	lines.push('};');
}
(async () => {
	lines.push('');
	lines.push('typedef struct { const char *cell, *set; } CALC_CELLSET;');
	lines.push('typedef struct { const char *range, *kind; int x, y, w, h; const char *xml; } CALC_CHART;');
	lines.push('typedef struct { const char *in, *out; } CALC_RECORD;');
	lines.push('');
	lines.push('static const CALC_CELLSET calc_chart_cells[] = {');
	for (const [r, v] of CHART_CELLS) lines.push('\t{ ' + cstr(r) + ', ' + cstr(v) + ' },');
	lines.push('};');
	lines.push('');
	lines.push('static const CALC_CHART calc_charts[] = {');
	for (const [rg, kind, x, y, w, h] of CHARTS) {
		lines.push('\t{ ' + cstr(rg) + ', ' + cstr(kind) + ', ' + [x, y, w, h].join(', ') + ',');
		lines.push('\t  ' + cstr(plugin_chart(CHART_CELLS, rg, kind, x, y, w, h)) + ' },');
	}
	lines.push('};');
	lines.push('');
	lines.push('static const CALC_RECORD calc_records[] = {');
	for (const r of RECORDS) {
		const name = /filename="([^"]*)"/.exec(r)[1];
		lines.push('\t{ ' + cstr(r) + ',');
		lines.push('\t  ' + cstr(await plugin_record(r, name)) + ' },');
	}
	lines.push('};');
	fs.writeFileSync(outPath, lines.join('\n') + '\n');
	console.log('calc_cases: ' + SHEETS.length + ' sheets, ' + OLD.length + ' conversions, ' + ADJUST.length +
		    ' moves, ' + CHARTS.length + ' charts, ' + RECORDS.length + ' records' +
		    (bad ? ', ' + bad + ' cells differ' : ''));
	process.exit(bad ? 1 : 0);
})();
