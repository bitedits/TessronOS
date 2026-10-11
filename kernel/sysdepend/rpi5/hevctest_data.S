/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hevctest_data.S
 *	What hevctest.c decodes, built into the kernel with HEVCTEST=1: the
 *	jobs of the decoder and the hashes their pictures are to have, as
 *	tools/hevc/mkjobs.py writes them. The path is from build_make, where
 *	make runs.
 */

	.section .rodata.hevctest, "a"

	.balign	8
	.global	kt_hevc_jobs
	.global	kt_hevc_jobs_end
kt_hevc_jobs:
	.incbin	"../tools/hevc/hevc_jobs.bin"
kt_hevc_jobs_end:
