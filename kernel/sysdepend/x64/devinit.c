/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	devinit.c (x64 PC)
 *	Device initialization hooks called by the kernel start-up sequence.
 */

#include <sys/machine.h>

#ifdef X64

#include "kernel.h"			/* first: the types as config.h sets them */
#include <sys/sysdef.h>
#include <tm/tmonitor.h>
#include <tk/device.h>
#include <ts/net.h>
#include <ts/pcie.h>
#include <ts/usb.h>
#include <ts/disp.h>
#include <ts/dp.h>
#include <ts/wm.h>
#include <ts/hid.h>
#include <ts/snd.h>
#include <ts/cam.h>
#include <ts/so.h>
#include <ts/ob.h>
#include "sysdepend.h"

IMPORT void	cp_simd_use( BOOL on );		/* outer_kernel/wm/compose.c */

EXPORT unsigned long long	knl_pcie_ecam_pa = 0;

/*
 * The bus as the firmware left it: the configuration space of segment 0
 * from the MCFG table, its functions' windows where the firmware put
 * them (PCIE_KEEP_FW_BARS)
 */
EXPORT INT knl_pcie_init( void )
{
	INT	i;

	for ( i = 0; i < knl_acpi.nmcfg; i++ ) {
		if ( knl_acpi.mcfg[i].seg == 0 ) {
			knl_pcie_ecam_pa = knl_acpi.mcfg[i].base;
			break;
		}
	}
	if ( knl_pcie_ecam_pa == 0 ) {
		tm_printf((UB *)"TessronOS x64: no MCFG: no PCI Express\n");
		return 0;
	}
	return knl_pcie_scan(0, 0);
}

/* Before the kernel starts (no kernel objects available yet) */
EXPORT ER knl_init_device( void )
{
	return E_OK;
}

/* From the initial task: register device drivers here */
EXPORT ER knl_start_device( void )
{
	/*
	 * The window compositor takes the plain loops of compose.c: the
	 * vector ones are written for AArch64 (compose_simd.c)
	 */
	cp_simd_use(FALSE);

	knl_pcie_init();	/* the PCI Express bus (design 10.4) */
	knl_vnet_init();	/* virtio-net on the bus (design 10.4) */
	knl_netdev_init();	/* and the same card as the device "neta" */
	knl_disp_init();	/* the screen (design 16.5) */
	knl_dp_init();	/* and drawing on it (design 16.4) */
	knl_wm_init();	/* and windows (design 16.5.5) */
	knl_hid_init();	/* keyboard and pointer (design 10.4) */
	knl_snd_init();	/* the sound device (design 10.15) */
	knl_cam_init();	/* the camera device (design 10.16) */
	knl_usb_init();	/* and the USB devices behind them (design 10.14) */
	so_main();		/* the protocol stack on top of it (design 12.6) */

	/* File management and the mount table (design 12.2.3) */
	if ( fs_main() >= E_OK ) {
		fs_regist(&knl_fimp_fat);
	}

	knl_rng_init();		/* random source and UUID v7 (design 11.4) */
	knl_proc_init();	/* process management (design 9.4) */
	knl_ob_init();		/* real objects of every kind (design 18) */
	knl_wmobj_init();	/* and windows among them (design 18.13) */
	knl_fs_posix_mounts();	/* "/", /dev and /tmp for the POSIX programs (design 5.8.1) */
	knl_start_init(CNF_INIT_PATH);	/* the first program, when there is one */
	return E_OK;
}

#if USE_SHUTDOWN
EXPORT ER knl_finish_device( void )
{
	return E_OK;
}
#endif /* USE_SHUTDOWN */

#endif /* X64 */
