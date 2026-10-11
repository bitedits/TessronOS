/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sysdef.h
 *	System dependencies definition (Broadcom BCM2711 / Raspberry Pi 4,
 *	Raspberry Pi 400, Compute Module 4)
 *	Included also from assembler program.
 *
 *	The addresses are those of the low peripheral mode the firmware sets
 *	up for a 64 bit kernel: the VideoCore bus addresses 0x7Exx_xxxx of
 *	the main peripherals appear at 0xFExx_xxxx, the ARM local block (GIC,
 *	local timer) at 0xFF80_0000, and the blocks on the SCB bus (PCIe,
 *	GENET) at 0xFDxx_xxxx. They and the interrupt numbers are those of
 *	the firmware's device trees (bcm2711-rpi-4-b.dtb, bcm2711-rpi-400.dtb,
 *	bcm2711-rpi-cm4.dtb) and docs/tessronos/03-platform.md 3.6.
 *	_PA is a physical address, _BASE the kernel virtual address of the
 *	same register block in the device window (DEV_BASE).
 */

#ifndef __SYS_SYSDEF_DEPEND_CPU_H__
#define __SYS_SYSDEF_DEPEND_CPU_H__

#include "../core/armv8a/sysdef.h"

/* ------------------------------------------------------------------------ */
/*
 * Physical memory map
 *	RAM starts at 0. The first range ends below 1GB where the VideoCore's
 *	share begins; a board with more than 1GB has a second range from 1GB
 *	up to the peripherals at 0xFC00_0000, and the 8GB board a third from
 *	4GB to 8GB. The device tree's /memory says which; the default below
 *	is only for a boot without one. 0..0x1000 holds the firmware's stub
 *	and the spin table the secondary cores wait on (/memreserve/).
 */
#define RAM_START		0x00000000
#define RAM_END_DEFAULT		0x3b400000		/* the first range of a board with the firmware's default share */

#define LOW_RESERVED_END	0x00001000		/* the firmware stub and the spin table: never handed out */

#define INTERNAL_RAM_START	RAM_START
#define INTERNAL_RAM_END	RAM_END_DEFAULT

/*
 * Memory a bus master other than the processors can reach. The VideoCore
 * (property mailbox, screen) and the EMMC2 controller of the first
 * stepping of the chip see only the low 1GB; the PCIe link (and with it
 * the VL805's xHCI) sees 3GB. Pages for DMA are therefore taken below
 * 1GB, which satisfies all of them; the page allocator's DMA zone ends
 * here instead of at 4GB.
 */
#define DMA_ZONE_LIMIT		0x40000000

/* ------------------------------------------------------------------------ */
/*
 * SoC peripherals (VideoCore bus 0x7E00_0000 at 0xFE00_0000)
 */
#define SOC_PERIPH_PA		0xFE000000

#define SYSTIMER_PA		0xFE003000		/* BCM2835-compatible 1MHz system timer */
#define VC_MAILBOX_PA		0xFE00B880		/* VideoCore mailbox (property interface) */
#define VC_MAILBOX_INTNO	(32 + 33)
#define PM_PA			0xFE100000		/* power management: watchdog and reset */
#define RNG_PA			0xFE104000		/* iproc-rng200 */
#define RNG_DT_PATH		"/soc/rng@7e104000"	/* used only when its node is not disabled */
#define GPIO_PA			0xFE200000		/* the 58 pins of the SoC */
#define GPIO_NPINS		58
#define GPIO_INTNO0		(32 + 113)		/* pins 0-27 */
#define GPIO_INTNO1		(32 + 114)		/* pins 28-57 */
#define GPIO_LED_ACT		42			/* the activity LED (Pi 400: its power LED), active high */

#define PL011_UART0_PA		0xFE201000		/* UART0: header pins 8 (TXD) and 10 (RXD) with dtoverlay=disable-bt */
#define PL011_UART0_INTNO	(32 + 121)		/* SPI 121 */
#define PL011_UART0_CLOCK	48000000		/* the firmware's UART clock */

#define EMMC2_PA		0xFE340000		/* EMMC2: the SD card slot, or the CM4's eMMC */
#define EMMC2_INTNO		(32 + 126)		/* SPI 126 */
#define SDHCI_PA		EMMC2_PA
#define SDHCI_INTNO		EMMC2_INTNO

/* ------------------------------------------------------------------------ */
/*
 * The SCB bus: the PCIe root complex and the Ethernet MAC
 */
#define PCIE_PA			0xFD500000		/* the root complex's register block (0x9310 bytes) */
#define PCIE_DT_PATH		"/scb/pcie@7d500000"	/* its node in the device tree */
#define PCIE_INTNO		(32 + 147)		/* its own interrupt ("pcie") */
#define PCIE_MSI_INTNO		(32 + 148)		/* its message interrupt controller ("msi") */
#define PCIE_INTA_SPI		143			/* INTA to INTD are SPI 143 to 146 */

/*
 * The windows of the link (the device tree's ranges and dma-ranges of
 * /scb/pcie@7d500000). Outbound: processor 0x6_0000_0000 and upwards is
 * PCIe 0xC000_0000 and upwards, 1GB, where the cards' registers are
 * placed. Inbound: PCIe 0 and upwards is physical 0 and upwards, 3GB.
 * kernel/sysdepend/rpi4/pcie_rc.c reads the device tree and uses these
 * only where it says nothing.
 */
#define PCIE_OUTBOUND_CPU_PA	0x600000000
#define PCIE_OUTBOUND_BUS_PA	0xC0000000
#define PCIE_OUTBOUND_SIZE	0x40000000
#define PCIE_INBOUND_BUS_PA	0x00000000
#define PCIE_INBOUND_SIZE	0xC0000000

/*
 * Configuration space is not one flat window: the root port is the foot
 * of the register block and a function below it is seen through a 4KB
 * aperture after its number is written to an index register
 * (pcie_rc.c gives kernel/sysman/pcie.c its two calls). A card's window
 * placed at PCIe address A is at processor address A + PCIE_BAR_CPU_OFFSET,
 * and a card reaches physical address P at PCIe address
 * P + PCIE_DMA_BUS_OFFSET; pcie_rc.c sets both from the device tree.
 */
#define PCIE_CFG_BOARD		1
#ifndef _in_asm_source_
extern __UINT64_TYPE__	knl_pcie_cpu_offset;
extern __UINT64_TYPE__	knl_pcie_dma_offset;
#endif
#define PCIE_BAR_CPU_OFFSET	(knl_pcie_cpu_offset)
#define PCIE_DMA_BUS_OFFSET	(knl_pcie_dma_offset)

#define GENET_PA		0xFD580000		/* GENET v5 (64KB) */
#define GENET_DT_PATH		"/scb/ethernet@7d580000"
#define GENET_INTNO0		(32 + 157)		/* SPI 157: the default queues, MDIO, link */
#define GENET_INTNO1		(32 + 158)		/* SPI 158: the priority queues */

/* ------------------------------------------------------------------------ */
/*
 * ARM local peripherals
 */
#define ARM_LOCAL_PA		0xFF800000
#define GICD_PA			0xFF841000		/* GIC-400 Distributor */
#define GICC_PA			0xFF842000		/* GIC-400 CPU interface */
#define GICH_PA			0xFF844000
#define GICV_PA			0xFF846000

#define DBG_UART_PA		PL011_UART0_PA		/* console used by boot.S (before the MMU) */

/*
 * The processor this image is for (MIDR part number: Cortex-A72), and
 * where boot.S says so when it finds another one: the debug UART of the
 * Raspberry Pi 5, the board a card of this family is most likely to
 * end up in by mistake
 */
#define BOOT_MIDR_PART		0xD08
#define BOOT_OTHER_UART_PA	0x107d001000
#define BOOT_IMAGE_FOR		"Raspberry Pi 4, 400 and Compute Module 4 (BCM2711)"

/* Kernel virtual addresses (device window) */
#define GICD_BASE		DEV_BASE(GICD_PA)
#define GICC_BASE		DEV_BASE(GICC_PA)
#define SYSTIMER_BASE		DEV_BASE(SYSTIMER_PA)
#define VC_MAILBOX_BASE		DEV_BASE(VC_MAILBOX_PA)
#define PM_BASE			DEV_BASE(PM_PA)
#define RNG_BASE		DEV_BASE(RNG_PA)
#define GPIO_BASE		DEV_BASE(GPIO_PA)
#define PL011_UART0_BASE	DEV_BASE(PL011_UART0_PA)
#define SDHCI_BASE		DEV_BASE(SDHCI_PA)
#define PCIE_BASE		DEV_BASE(PCIE_PA)
#define GENET_BASE		DEV_BASE(GENET_PA)
#define DBG_UART_BASE		DEV_BASE(DBG_UART_PA)

/* ------------------------------------------------------------------------ */
/*
 * Interrupts
 */
#define N_INTVEC		512			/* GIC-400: up to 480 SPIs */
#define TIMER_INTNO		30			/* Generic Timer, non-secure EL1 physical (PPI 14) */
#define VTIMER_INTNO		27			/* Generic Timer, virtual (PPI 11) */

/* Generic Timer frequency: read CNTFRQ_EL0 at boot (54MHz on the Raspberry Pi 4) */
#define CNTFRQ_DEFAULT		54000000

#endif /* __SYS_SYSDEF_DEPEND_CPU_H__ */
