/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	dtb.c (ARMv8-A / AArch64)
 *	Minimal flattened device tree (FDT) reader for the boot sequence.
 *
 *	Reads only what the kernel needs before the memory manager exists:
 *	  /              compatible and model: which board this is (board.c)
 *	  /memory        reg (one or more ranges; #address-cells/#size-cells of the root)
 *	  /chosen        bootargs, stdout-path
 *	  /psci          method ("smc" or "hvc")
 *	  /cpus/cpu@*    number of processors, and how each is started: PSCI,
 *	                 or a spin table with the address it waits on
 *	  /pl011@*       the PL011 UARTs: where they are and their interrupt
 *	  simple-framebuffer, at any depth: a screen the firmware set up
 *	  /memreserve/   the ranges of the header's reservation block, which
 *	                 the firmware keeps for itself (the Raspberry Pi 4's
 *	                 stub and spin table at 0..0x1000)
 *	Everything is read in place: no allocation, no copies except the strings.
 *
 *	Later, once the kernel runs, a driver can ask for one property of one
 *	node by its path (knl_dtb_prop) and for an entry of a ranges or
 *	dma-ranges property, decoded with the cells its node and the parent
 *	say (knl_dtb_ranges). The device tree stays where the firmware put it:
 *	the page allocator keeps its pages out of use.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_ARMV8A

#include "kernel.h"
#include "../../../sysdepend.h"

#define FDT_MAGIC		0xd00dfeed
#define FDT_BEGIN_NODE		1
#define FDT_END_NODE		2
#define FDT_PROP		3
#define FDT_NOP			4
#define FDT_END			9

EXPORT T_MEMRANGE	knl_mem_range[KNL_MAX_MEMRANGE];
EXPORT INT		knl_mem_range_n = 0;
EXPORT INT		knl_num_cpu = 0;
EXPORT UD		knl_cpu_mpidr[KNL_MAX_CPU];	/* reg of /cpus/cpu@* (MPIDR affinity) */
EXPORT UW		knl_psci_method = PSCI_METHOD_UNKNOWN;
EXPORT UB		knl_bootargs[KNL_BOOTARGS_LEN];
EXPORT UB		knl_stdout_path[KNL_STDOUT_LEN];
EXPORT UD		knl_dtb_size = 0;
EXPORT UD		knl_pl011_pa[KNL_MAX_PL011];	/* reg of /pl011@* */
EXPORT UW		knl_pl011_int[KNL_MAX_PL011];	/* its interrupt, 0 when none */
EXPORT INT		knl_pl011_n = 0;
EXPORT T_DTBFB		knl_dtb_fb;
EXPORT UB		knl_dtb_compat[KNL_COMPAT_LEN];
EXPORT INT		knl_dtb_compat_len = 0;
EXPORT UB		knl_dtb_model[KNL_MODEL_LEN];
EXPORT T_MEMRANGE	knl_mem_rsv[KNL_MAX_MEMRSV];	/* /memreserve/ */
EXPORT INT		knl_mem_rsv_n = 0;
EXPORT UB		knl_cpu_enable[KNL_MAX_CPU];	/* CPU_ENABLE_* of each /cpus/cpu@* */
EXPORT UD		knl_cpu_release[KNL_MAX_CPU];	/* its cpu-release-addr (spin table) */

/* How deep the cells of the nodes are followed; a framebuffer is no deeper */
#define DEPTH_MAX		8

/* big-endian access */
LOCAL UW be32( const UB *p )
{
	return ((UW)p[0] << 24) | ((UW)p[1] << 16) | ((UW)p[2] << 8) | (UW)p[3];
}
LOCAL UD be_cells( const UB *p, UW cells )
{
	UD	v = 0;
	while ( cells-- > 0 ) {
		v = (v << 32) | be32(p);
		p += 4;
	}
	return v;
}

LOCAL BOOL str_eq( const char *a, const char *b )
{
	while ( *a != '\0' && *a == *b ) {
		a++;
		b++;
	}
	return ( *a == *b );
}
LOCAL BOOL str_eq_n( const char *a, const char *b, INT n )	/* a == b for the first n chars of a */
{
	while ( n-- > 0 ) {
		if ( *a != *b ) return FALSE;
		if ( *a == '\0' ) return TRUE;
		a++;
		b++;
	}
	return TRUE;
}
LOCAL void str_copy( UB *dst, const UB *src, INT len, INT max )
{
	INT	i;
	if ( len > max - 1 ) len = max - 1;
	for ( i = 0; i < len; i++ ) {
		dst[i] = src[i];
	}
	dst[len] = '\0';
}

/* Whether a string list (compatible) holds a string */
LOCAL BOOL list_has( const UB *list, UW len, const char *want )
{
	UW	at = 0;

	while ( at < len ) {
		const char	*one = (const char *)list + at;

		if ( str_eq(one, want) ) {
			return TRUE;
		}
		while ( at < len && list[at] != '\0' ) at++;
		at++;
	}
	return FALSE;
}

/*
 * Node name matcher: "memory" matches "memory" and "memory@0"
 */
LOCAL BOOL node_is( const char *name, const char *base )
{
	while ( *base != '\0' ) {
		if ( *name++ != *base++ ) return FALSE;
	}
	return ( *name == '\0' || *name == '@' );
}

/*
 * The root node's own properties come before its first child, so the
 * walk stops there. The compatible is kept as the list it is, NULs and
 * all, cut at the last whole string that fits.
 */
EXPORT ER knl_dtb_root( CONST UB *fdt, UB *compat, INT cmax, INT *p_clen, UB *model, INT mmax )
{
	CONST UB	*p, *strs, *pval;
	UW		token, len;
	INT		depth = 0;

	*p_clen = 0;
	if ( mmax > 0 ) model[0] = '\0';
	if ( fdt == NULL || be32(fdt) != FDT_MAGIC ) {
		return E_NOEXS;
	}
	p = fdt + be32(fdt + 8);
	strs = fdt + be32(fdt + 12);

	for ( ;; ) {
		token = be32(p);
		p += 4;
		switch ( token ) {
		case FDT_BEGIN_NODE:
			if ( ++depth > 1 ) {
				return E_OK;		/* the first child: no more of the root's */
			}
			while ( *p != '\0' ) p++;
			p = (CONST UB *)(((UBINT)p + 4) & ~(UBINT)3);
			break;
		case FDT_PROP:
			len = be32(p);
			pval = p + 8;
			if ( depth == 1 ) {
				CONST char *pname = (CONST char *)strs + be32(p + 4);

				if ( str_eq(pname, "compatible") ) {
					INT	n = (INT)len;

					if ( n > cmax ) {
						n = cmax;
						while ( n > 0 && pval[n - 1] != '\0' ) {
							n--;	/* back to the end of a whole string */
						}
					}
					knl_memcpy(compat, pval, (SZ)n);
					*p_clen = n;
				} else if ( str_eq(pname, "model") && mmax > 0 ) {
					str_copy(model, pval, (INT)len, mmax);
				}
			}
			p = (CONST UB *)(((UBINT)pval + len + 3) & ~(UBINT)3);
			break;
		case FDT_NOP:
			break;
		default:			/* the end, or the root closed */
			return E_OK;
		}
	}
}

EXPORT ER knl_dtb_init( UBINT dtb )
{
	const UB	*fdt = (const UB *)dtb;
	const UB	*p, *strs;
	UW		token, len, nameoff;
	INT		depth = 0;
	INT		in_chosen = 0, in_psci = 0, in_cpus = 0, in_memory = 0, in_cpu = 0;
	INT		in_uart = 0;
	INT		cpu_idx = 0, ncpu_nodes = 0;
	UW		addr_cells = 2, size_cells = 1;	/* FDT defaults */
	UW		acells[DEPTH_MAX + 1], scells[DEPTH_MAX + 1];
	const char	*name, *pname;
	const UB	*pval;
	T_DTBFB		fb;			/* the node being read */
	BOOL		fb_compat = FALSE, fb_on = TRUE;
	INT		fb_depth = 0;

	knl_memset(&knl_dtb_fb, 0, sizeof(knl_dtb_fb));
	knl_mem_range_n = 0;
	knl_pl011_n = 0;
	knl_bootargs[0] = '\0';
	knl_stdout_path[0] = '\0';

	knl_dtb_compat_len = 0;
	knl_dtb_model[0] = '\0';

	if ( dtb == 0 || be32(fdt) != FDT_MAGIC ) {
		return E_NOEXS;
	}
	knl_dtb_size = be32(fdt + 4);
	(void)knl_dtb_root(fdt, knl_dtb_compat, KNL_COMPAT_LEN, &knl_dtb_compat_len,
			   knl_dtb_model, KNL_MODEL_LEN);

	/*
	 * The reservation block: pairs of 64 bit address and size, ended by
	 * a pair of zeros. The firmware lists what it keeps for itself.
	 */
	knl_mem_rsv_n = 0;
	{
		const UB	*r = fdt + be32(fdt + 16);	/* off_mem_rsvmap */

		while ( knl_mem_rsv_n < KNL_MAX_MEMRSV ) {
			UD	a = be_cells(r, 2), s = be_cells(r + 8, 2);

			if ( a == 0 && s == 0 ) break;
			knl_mem_rsv[knl_mem_rsv_n].start = a;
			knl_mem_rsv[knl_mem_rsv_n].size  = s;
			knl_mem_rsv_n++;
			r += 16;
		}
	}
	for ( cpu_idx = 0; cpu_idx < KNL_MAX_CPU; cpu_idx++ ) {
		knl_cpu_enable[cpu_idx] = CPU_ENABLE_PSCI;
		knl_cpu_release[cpu_idx] = 0;
	}
	cpu_idx = 0;
	p = fdt + be32(fdt + 8);		/* off_dt_struct */
	strs = fdt + be32(fdt + 12);		/* off_dt_strings */

	while (1) {
		token = be32(p);
		p += 4;
		switch ( token ) {
		case FDT_BEGIN_NODE:
			name = (const char *)p;
			while ( *p != '\0' ) p++;	/* node name */
			p = (const UB *)(((UBINT)p + 4) & ~(UBINT)3);
			depth++;
			if ( depth <= DEPTH_MAX ) {	/* what this node's children use until it says */
				acells[depth] = 2;
				scells[depth] = 1;
			}
			knl_memset(&fb, 0, sizeof(fb));
			fb_compat = FALSE;
			fb_on = TRUE;
			fb_depth = depth;
			if ( depth == 2 ) {
				in_chosen = node_is(name, "chosen");
				in_psci   = node_is(name, "psci");
				in_cpus   = node_is(name, "cpus");
				in_memory = node_is(name, "memory");
				in_uart   = node_is(name, "pl011") && knl_pl011_n < KNL_MAX_PL011;
				if ( in_uart ) {
					knl_pl011_pa[knl_pl011_n] = 0;
					knl_pl011_int[knl_pl011_n] = 0;
					knl_pl011_n++;
				}
			} else if ( depth == 3 && in_cpus ) {
				in_cpu = node_is(name, "cpu");
				if ( in_cpu ) cpu_idx = ncpu_nodes++;
			}
			break;

		case FDT_END_NODE:
			if ( depth == fb_depth && fb_compat && fb_on && knl_dtb_fb.size == 0
			  && fb.pa != 0 && fb.width != 0 && fb.height != 0 && fb.stride != 0 ) {
				if ( fb.size < (UD)fb.stride * fb.height ) {
					fb.size = (UD)fb.stride * fb.height;
				}
				knl_dtb_fb = fb;
			}
			fb_depth = 0;
			if ( depth == 2 ) {
				in_chosen = in_psci = in_cpus = in_memory = in_uart = 0;
			} else if ( depth == 3 ) {
				in_cpu = 0;
			}
			depth--;
			break;

		case FDT_PROP:
			len = be32(p);
			nameoff = be32(p + 4);
			pval = p + 8;
			pname = (const char *)strs + nameoff;
			p = (const UB *)(((UBINT)pval + len + 3) & ~(UBINT)3);

			if ( depth <= DEPTH_MAX ) {
				if ( str_eq(pname, "#address-cells") ) acells[depth] = be32(pval);
				else if ( str_eq(pname, "#size-cells") ) scells[depth] = be32(pval);
			}
			if ( depth == fb_depth && depth >= 2 && depth <= DEPTH_MAX ) {
				if ( str_eq(pname, "compatible") ) {
					fb_compat = list_has(pval, len, "simple-framebuffer");
				} else if ( str_eq(pname, "status") ) {
					fb_on = str_eq_n((const char *)pval, "ok", 2);
				} else if ( str_eq(pname, "reg")
					 && len >= ( acells[depth - 1] + scells[depth - 1] ) * 4 ) {
					fb.pa = be_cells(pval, acells[depth - 1]);
					fb.size = be_cells(pval + acells[depth - 1] * 4, scells[depth - 1]);
				} else if ( str_eq(pname, "width") && len >= 4 ) {
					fb.width = be32(pval);
				} else if ( str_eq(pname, "height") && len >= 4 ) {
					fb.height = be32(pval);
				} else if ( str_eq(pname, "stride") && len >= 4 ) {
					fb.stride = be32(pval);
				} else if ( str_eq(pname, "format") ) {
					str_copy(fb.format, pval, (INT)len, KNL_FB_FORMAT_LEN);
				}
			}
			if ( depth == 1 ) {			/* root node */
				if ( str_eq(pname, "#address-cells") ) addr_cells = be32(pval);
				else if ( str_eq(pname, "#size-cells") ) size_cells = be32(pval);
			} else if ( in_memory && str_eq(pname, "reg") ) {
				UW	entry = (addr_cells + size_cells) * 4;
				while ( len >= entry && knl_mem_range_n < KNL_MAX_MEMRANGE ) {
					UD	start = be_cells(pval, addr_cells);
					UD	size  = be_cells(pval + addr_cells * 4, size_cells);
					if ( size != 0 ) {
						knl_mem_range[knl_mem_range_n].start = start;
						knl_mem_range[knl_mem_range_n].size  = size;
						knl_mem_range_n++;
					}
					pval += entry;
					len -= entry;
				}
			} else if ( in_chosen ) {
				if ( str_eq(pname, "bootargs") ) {
					str_copy(knl_bootargs, pval, (INT)len, KNL_BOOTARGS_LEN);
				} else if ( str_eq(pname, "stdout-path") ) {
					str_copy(knl_stdout_path, pval, (INT)len, KNL_STDOUT_LEN);
				}
			} else if ( in_uart && depth == 2 && str_eq(pname, "reg") ) {
				knl_pl011_pa[knl_pl011_n - 1] = be_cells(pval, addr_cells);
			} else if ( in_uart && depth == 2 && str_eq(pname, "interrupts") && len >= 12 ) {
				/* GIC: kind (0 SPI, 1 PPI), number, flags */
				knl_pl011_int[knl_pl011_n - 1] = be32(pval + 4) + ( ( be32(pval) == 0 ) ? 32 : 16 );
			} else if ( in_psci && str_eq(pname, "method") ) {
				if ( str_eq_n((const char *)pval, "smc", 3) ) knl_psci_method = PSCI_METHOD_SMC;
				else if ( str_eq_n((const char *)pval, "hvc", 3) ) knl_psci_method = PSCI_METHOD_HVC;
			} else if ( in_cpu && str_eq(pname, "device_type") ) {
				if ( str_eq((const char *)pval, "cpu") ) knl_num_cpu++;
			} else if ( in_cpu && str_eq(pname, "reg") && cpu_idx < KNL_MAX_CPU ) {
				knl_cpu_mpidr[cpu_idx] = be_cells(pval, len / 4);
			} else if ( in_cpu && str_eq(pname, "enable-method") && cpu_idx < KNL_MAX_CPU ) {
				knl_cpu_enable[cpu_idx] = list_has(pval, len, "spin-table")
							? CPU_ENABLE_SPIN : CPU_ENABLE_PSCI;
			} else if ( in_cpu && str_eq(pname, "cpu-release-addr") && cpu_idx < KNL_MAX_CPU
				 && len >= 4 && len <= 8 ) {
				knl_cpu_release[cpu_idx] = be_cells(pval, len / 4);
			}
			break;

		case FDT_NOP:
			break;

		case FDT_END:
		default:
			return ( knl_mem_range_n > 0 ) ? E_OK : E_NOEXS;
		}
	}
}

/* ------------------------------------------------------------------------ */
/*
 * One property of one node, for the drivers once the kernel runs
 */

/*
 * Whether a node's name is the path's component (n bytes): the same, or
 * the same before the unit address when the component names none.
 */
LOCAL BOOL comp_is( const char *name, const char *comp, INT n )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( name[i] != comp[i] ) return FALSE;
	}
	if ( name[n] == '\0' ) return TRUE;
	for ( i = 0; i < n; i++ ) {
		if ( comp[i] == '@' ) return FALSE;
	}
	return ( name[n] == '@' );
}

typedef struct {
	const UB	*val;		/* the property, NULL when the node lacks it */
	INT		len;
	UW		ac, sc;		/* the node's own #address-cells and #size-cells */
	UW		pac;		/* its parent's #address-cells */
} DTPROP;

/*
 * Walk to the node of 'path' ("/emmc2bus", "/scb/pcie@7d500000") and
 * take 'prop' from it, with the cells that decode it. FALSE when there
 * is no device tree or no such node.
 */
LOCAL BOOL dtb_find( const char *path, const char *prop, DTPROP *r )
{
	const UB	*fdt, *p, *strs;
	const char	*comp[DEPTH_MAX];
	INT		clen[DEPTH_MAX];
	INT		ncomp = 0, depth = 0, matched = 0;
	UW		acells[DEPTH_MAX + 2], scells[DEPTH_MAX + 2];
	UW		token, len;

	r->val = NULL;
	r->len = 0;
	r->ac = 2;
	r->sc = 1;
	r->pac = 2;
	if ( knl_dtb_addr == 0 ) return FALSE;
	fdt = (const UB *)PA2VA(knl_dtb_addr);
	if ( be32(fdt) != FDT_MAGIC ) return FALSE;

	/* the path's components */
	while ( *path != '\0' ) {
		while ( *path == '/' ) path++;
		if ( *path == '\0' ) break;
		if ( ncomp >= DEPTH_MAX - 1 ) return FALSE;
		comp[ncomp] = path;
		while ( *path != '\0' && *path != '/' ) path++;
		clen[ncomp] = (INT)( path - comp[ncomp] );
		ncomp++;
	}

	p = fdt + be32(fdt + 8);
	strs = fdt + be32(fdt + 12);
	for (;;) {
		token = be32(p);
		p += 4;
		switch ( token ) {
		case FDT_BEGIN_NODE: {
			const char	*name = (const char *)p;

			while ( *p != '\0' ) p++;
			p = (const UB *)(((UBINT)p + 4) & ~(UBINT)3);
			if ( depth == ncomp + 1 && matched == ncomp ) {
				return TRUE;	/* the node's first child: its properties are all read */
			}
			depth++;
			if ( depth <= DEPTH_MAX + 1 ) {
				acells[depth] = 2;
				scells[depth] = 1;
			}
			if ( depth >= 2 && matched == depth - 2 && depth - 1 <= ncomp
			  && comp_is(name, comp[depth - 2], clen[depth - 2]) ) {
				matched = depth - 1;
			}
			break;
		}
		case FDT_END_NODE:
			if ( depth == ncomp + 1 && matched == ncomp ) {
				return TRUE;
			}
			if ( depth >= 2 && matched >= depth - 1 ) {
				matched = depth - 2;
			}
			depth--;
			break;
		case FDT_PROP: {
			const UB	*pval;
			const char	*pname;

			len = be32(p);
			pname = (const char *)strs + be32(p + 4);
			pval = p + 8;
			p = (const UB *)(((UBINT)pval + len + 3) & ~(UBINT)3);
			if ( depth <= DEPTH_MAX + 1 ) {
				if ( str_eq(pname, "#address-cells") && len >= 4 ) acells[depth] = be32(pval);
				else if ( str_eq(pname, "#size-cells") && len >= 4 ) scells[depth] = be32(pval);
			}
			if ( depth == ncomp + 1 && matched == ncomp ) {
				if ( str_eq(pname, prop) ) {
					r->val = pval;
					r->len = (INT)len;
				}
				r->ac = acells[depth];
				r->sc = scells[depth];
				r->pac = ( depth >= 2 ) ? acells[depth - 1] : 2;
			}
			break;
		}
		case FDT_NOP:
			break;
		default:
			return FALSE;
		}
	}
}

EXPORT CONST UB *knl_dtb_prop( CONST char *path, CONST char *prop, INT *p_len )
{
	DTPROP	r;

	if ( !dtb_find(path, prop, &r) || r.val == NULL ) {
		if ( p_len != NULL ) *p_len = 0;
		return NULL;
	}
	if ( p_len != NULL ) *p_len = r.len;
	return r.val;
}

/*
 * Whether the node at 'path' is there and in use: its status is "okay"
 * or "ok", or it has none. A machine that does not have a block the
 * firmware's device tree names may mark it "disabled" (QEMU's raspi4b
 * does so for the PCIe, the Ethernet and the random generator), and its
 * registers may then not answer at all.
 */
EXPORT BOOL knl_dtb_okay( CONST char *path )
{
	DTPROP	r;

	if ( !dtb_find(path, "status", &r) ) {
		return FALSE;
	}
	return ( r.val == NULL || str_eq_n((const char *)r.val, "ok", 2) );
}

EXPORT UW knl_dtb_u32( CONST char *path, CONST char *prop, UW dflt )
{
	INT		len;
	CONST UB	*v = knl_dtb_prop(path, prop, &len);

	return ( v != NULL && len >= 4 ) ? be32(v) : dflt;
}

/*
 * Entry 'index' of a ranges or dma-ranges property: the child's address
 * in the node's own cells, the parent's in its parent's, the size in the
 * node's. A PCI address is three cells, the first of them its kind; only
 * the low two, the address itself, are kept.
 */
EXPORT ER knl_dtb_ranges( CONST char *path, CONST char *prop, INT index,
			  UD *p_child, UD *p_parent, UD *p_size )
{
	DTPROP	r;
	INT	entry;

	if ( !dtb_find(path, prop, &r) || r.val == NULL ) {
		return E_NOEXS;
	}
	entry = (INT)( r.ac + r.pac + r.sc ) * 4;
	if ( entry <= 0 || r.ac > 3 || r.pac > 3 || r.sc > 2 || r.len < entry * ( index + 1 ) ) {
		return E_NOEXS;
	}
	r.val += entry * index;
	if ( p_child != NULL )  *p_child  = be_cells(r.val, r.ac);
	if ( p_parent != NULL ) *p_parent = be_cells(r.val + r.ac * 4, r.pac);
	if ( p_size != NULL )   *p_size   = be_cells(r.val + ( r.ac + r.pac ) * 4, r.sc);
	return E_OK;
}

#endif /* CPU_CORE_ARMV8A */
