/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	calc_js.S
 *	基本表計算: the scripts the V8 engine runs, carried in the program
 *	(calc_v8.cc). Assembled with -I application/calc/js.
 */

	.section .rodata
	.p2align 4
	.global	calc_js_jstat
	.global	calc_js_jstat_end
calc_js_jstat:
	.incbin	"jstat.min.js"
calc_js_jstat_end:
	.byte	0

	.p2align 4
	.global	calc_js_formula
	.global	calc_js_formula_end
calc_js_formula:
	.incbin	"formula.min.js"
calc_js_formula_end:
	.byte	0

	.p2align 4
	.global	calc_js_engine
	.global	calc_js_engine_end
calc_js_engine:
	.incbin	"calc_engine.js"
calc_js_engine_end:
	.byte	0
