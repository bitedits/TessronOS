/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_speech_data.S
 *	What test_blink_speech hands the microphone, built into the kernel:
 *	the English sentence "Hello world, this is a test." said by a
 *	speech synthesizer (Flite, the voice cmu_us_slt), 16-bit samples at
 *	16 kHz in mono, 2.2 seconds. The path is from build_make, where make
 *	runs.
 */

	.section .rodata.ktest_speech, "a"

	.balign	8
	.global	kt_speech_hello
	.global	kt_speech_hello_end
kt_speech_hello:
	.incbin	"../tests/ktest/data/speech_hello.raw"
kt_speech_hello_end:
