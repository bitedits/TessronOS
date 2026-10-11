#!/bin/bash
#
# chk_isa.sh -- look for instructions beyond ARMv8.0-A (with CRC32) in
# AArch64 objects and images
#
# Copyright (C) 2026 satromi
# This software is distributed under the T-License 2.2.
#
# The Raspberry Pi 4 family's Cortex-A72 is ARMv8.0-A: it has no LSE
# atomics (ldadd, cas, swp ...), no load-acquire RCpc (ldapr), no dot
# product, and its cryptographic extension is optional (the BCM2711 has
# none). An instruction of these traps as undefined there, so every
# object linked into a TARGET=_RPI4_ image is disassembled and searched.
#
# The out-of-line atomic helpers of libgcc (__aarch64_cas4_acq and the
# like) hold both forms and choose at run time by a flag that stays clear
# on this system; they are listed apart and do not count as findings.
#
# Usage: chk_isa.sh <file> ...     exit 1 when anything is found
#
OBJDUMP=${OBJDUMP:-aarch64-none-elf-objdump}
PAT='^(ldadd|ldclr|ldeor|ldset|ldsmax|ldsmin|ldumax|ldumin|swp|cas|casp|stadd|stclr|steor|stset|stsmax|stsmin|stumax|stumin|ldapr|ldapur|stlur|aese|aesd|aesmc|aesimc|sha1[a-z0-9]*|sha256[a-z0-9]*|sha512[a-z0-9]*|eor3|rax1|xar|bcax|sm3[a-z0-9]*|sm4[a-z0-9]*|pmull2?|sdot|udot|usdot|fmlal2?|fmlsl2?|bfmmla|bfdot)[a-z0-9]*$'
bad=0
for f in "$@"; do
	"$OBJDUMP" -d --no-show-raw-insn "$f" 2>/dev/null | awk -v pat="$PAT" -v file="$f" '
		/^[0-9a-f]+ <.*>:$/ { fn = $2; next }
		/^ *[0-9a-f]+:\t/ {
			split($0, a, "\t"); m = a[2]; sub(/[ \t].*/, "", m); sub(/\..*/, "", m)
			if (m ~ pat) {
				if (fn ~ /^<__aarch64_(cas|swp|ldadd|ldclr|ldeor|ldset)/) { helper[fn]++; next }
				n++; if (n <= 20) printf "%s: %s %s\n", file, fn, a[2]
			}
		}
		END {
			for (h in helper) hc++
			if (hc) printf "%s: %d libgcc atomic helper(s) with both forms (chosen at run time)\n", file, hc
			if (n) { printf "%s: %d instruction(s) beyond ARMv8.0-A\n", file, n; exit 1 }
		}' || bad=1
done
exit $bad
