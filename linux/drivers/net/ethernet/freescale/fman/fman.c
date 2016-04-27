#include <machine/rtems-bsd-kernel-space.h>

#include <rtems/bsd/local/opt_dpaa.h>

/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
//  *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "fman.h"
#include "fman_muram.h"
#include <asm/mpc85xx.h>

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#ifdef __rtems__
#include <bsp/fdt.h>
#include <bsp/qoriq.h>
#endif /* __rtems__ */


/* General defines */
#define FMAN_LIODN_TBL			64	/* size of LIODN table */
#define MAX_NUM_OF_MACS			10
#define FM_NUM_OF_FMAN_CTRL_EVENT_REGS	4
#define BASE_RX_PORTID			0x08
#define BASE_TX_PORTID			0x28

/* Modules registers offsets */
#define BMI_OFFSET		0x00080000
#define QMI_OFFSET		0x00080400
#define DMA_OFFSET		0x000C2000
#define FPM_OFFSET		0x000C3000
#define IMEM_OFFSET		0x000C4000
#define CGP_OFFSET		0x000DB000

/* Exceptions bit map */
#define EX_DMA_BUS_ERROR		0x80000000
#define EX_DMA_READ_ECC			0x40000000
#define EX_DMA_SYSTEM_WRITE_ECC	0x20000000
#define EX_DMA_FM_WRITE_ECC		0x10000000
#define EX_FPM_STALL_ON_TASKS		0x08000000
#define EX_FPM_SINGLE_ECC		0x04000000
#define EX_FPM_DOUBLE_ECC		0x02000000
#define EX_QMI_SINGLE_ECC		0x01000000
#define EX_QMI_DEQ_FROM_UNKNOWN_PORTID	0x00800000
#define EX_QMI_DOUBLE_ECC		0x00400000
#define EX_BMI_LIST_RAM_ECC		0x00200000
#define EX_BMI_STORAGE_PROFILE_ECC	0x00100000
#define EX_BMI_STATISTICS_RAM_ECC	0x00080000
#define EX_IRAM_ECC			0x00040000
#define EX_MURAM_ECC			0x00020000
#define EX_BMI_DISPATCH_RAM_ECC	0x00010000
#define EX_DMA_SINGLE_PORT_ECC		0x00008000

#define DFLT_EXCEPTIONS	\
	 ((EX_DMA_BUS_ERROR)            | \
	  (EX_DMA_READ_ECC)              | \
	  (EX_DMA_SYSTEM_WRITE_ECC)      | \
	  (EX_DMA_FM_WRITE_ECC)          | \
	  (EX_FPM_STALL_ON_TASKS)        | \
	  (EX_FPM_SINGLE_ECC)            | \
	  (EX_FPM_DOUBLE_ECC)            | \
	  (EX_QMI_DEQ_FROM_UNKNOWN_PORTID) | \
	  (EX_BMI_LIST_RAM_ECC)          | \
	  (EX_BMI_STORAGE_PROFILE_ECC)   | \
	  (EX_BMI_STATISTICS_RAM_ECC)    | \
	  (EX_MURAM_ECC)                 | \
	  (EX_BMI_DISPATCH_RAM_ECC)      | \
	  (EX_QMI_DOUBLE_ECC)            | \
	  (EX_QMI_SINGLE_ECC))

/* DMA defines */
/* masks */
#define DMA_MODE_AID_OR			0x20000000
#define DMA_MODE_SBER			0x10000000
#define DMA_MODE_BER			0x00200000
#define DMA_MODE_ECC			0x00000020
#define DMA_MODE_SECURE_PROT		0x00000800
#define DMA_MODE_EMER_READ		0x00080000
#define DMA_MODE_AXI_DBG_MASK		0x0F000000

#define DMA_TRANSFER_PORTID_MASK	0xFF000000
#define DMA_TRANSFER_TNUM_MASK		0x00FF0000
#define DMA_TRANSFER_LIODN_MASK	0x00000FFF

#define DMA_STATUS_BUS_ERR		0x08000000
#define DMA_STATUS_READ_ECC		0x04000000
#define DMA_STATUS_SYSTEM_WRITE_ECC	0x02000000
#define DMA_STATUS_FM_WRITE_ECC	0x01000000
#define DMA_STATUS_FM_SPDAT_ECC	0x00080000

#define DMA_MODE_CACHE_OR_SHIFT		30
#define DMA_MODE_AXI_DBG_SHIFT			24
#define DMA_MODE_CEN_SHIFT			13
#define DMA_MODE_CEN_MASK			0x00000007
#define DMA_MODE_DBG_SHIFT			7
#define DMA_MODE_EMER_LVL_SHIFT		6
#define DMA_MODE_AID_MODE_SHIFT		4

#define DMA_THRESH_COMMQ_SHIFT			24
#define DMA_THRESH_READ_INT_BUF_SHIFT		16
#define DMA_THRESH_READ_INT_BUF_MASK		0x0000003f
#define DMA_THRESH_WRITE_INT_BUF_MASK		0x0000003f

#define DMA_TRANSFER_PORTID_SHIFT		24
#define DMA_TRANSFER_TNUM_SHIFT		16

#define DMA_CAM_SIZEOF_ENTRY			0x40
#define DMA_CAM_UNITS				8

#define DMA_LIODN_SHIFT		16
#define DMA_LIODN_BASE_MASK	0x00000FFF

/* FPM defines */
#define FPM_EV_MASK_DOUBLE_ECC		0x80000000
#define FPM_EV_MASK_STALL		0x40000000
#define FPM_EV_MASK_SINGLE_ECC		0x20000000
#define FPM_EV_MASK_RELEASE_FM		0x00010000
#define FPM_EV_MASK_DOUBLE_ECC_EN	0x00008000
#define FPM_EV_MASK_STALL_EN		0x00004000
#define FPM_EV_MASK_SINGLE_ECC_EN	0x00002000
#define FPM_EV_MASK_EXTERNAL_HALT	0x00000008
#define FPM_EV_MASK_ECC_ERR_HALT	0x00000004

#define FPM_RAM_MURAM_ECC		0x00008000
#define FPM_RAM_IRAM_ECC		0x00004000
#define FPM_RAM_MURAM_TEST_ECC		0x20000000
#define FPM_RAM_IRAM_TEST_ECC		0x10000000
#define FPM_IRAM_ECC_ERR_EX_EN		0x00020000
#define FPM_MURAM_ECC_ERR_EX_EN	0x00040000
#define FPM_RAM_IRAM_ECC_EN		0x40000000
#define FPM_RAM_RAMS_ECC_EN		0x80000000
#define FPM_RAM_RAMS_ECC_EN_SRC_SEL	0x08000000

#define FPM_REV1_MAJOR_MASK		0x0000FF00
#define FPM_REV1_MINOR_MASK		0x000000FF

#define FPM_DISP_LIMIT_SHIFT		24

#define FPM_PRT_FM_CTL1			0x00000001
#define FPM_PRT_FM_CTL2			0x00000002
#define FPM_PORT_FM_CTL_PORTID_SHIFT	24
#define FPM_PRC_ORA_FM_CTL_SEL_SHIFT	16

#define FPM_THR1_PRS_SHIFT		24
#define FPM_THR1_KG_SHIFT		16
#define FPM_THR1_PLCR_SHIFT		8
#define FPM_THR1_BMI_SHIFT		0

#define FPM_THR2_QMI_ENQ_SHIFT		24
#define FPM_THR2_QMI_DEQ_SHIFT		0
#define FPM_THR2_FM_CTL1_SHIFT		16
#define FPM_THR2_FM_CTL2_SHIFT		8

#define FPM_EV_MASK_CAT_ERR_SHIFT	1
#define FPM_EV_MASK_DMA_ERR_SHIFT	0

#define FPM_REV1_MAJOR_SHIFT		8

#define FPM_RSTC_FM_RESET		0x80000000
#define FPM_RSTC_MAC0_RESET		0x40000000
#define FPM_RSTC_MAC1_RESET		0x20000000
#define FPM_RSTC_MAC2_RESET		0x10000000
#define FPM_RSTC_MAC3_RESET		0x08000000
#define FPM_RSTC_MAC8_RESET		0x04000000
#define FPM_RSTC_MAC4_RESET		0x02000000
#define FPM_RSTC_MAC5_RESET		0x01000000
#define FPM_RSTC_MAC6_RESET		0x00800000
#define FPM_RSTC_MAC7_RESET		0x00400000
#define FPM_RSTC_MAC9_RESET		0x00200000

#define FPM_TS_INT_SHIFT		16
#define FPM_TS_CTL_EN			0x80000000

/* BMI defines */
#define BMI_INIT_START				0x80000000
#define BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC	0x80000000
#define BMI_ERR_INTR_EN_LIST_RAM_ECC		0x40000000
#define BMI_ERR_INTR_EN_STATISTICS_RAM_ECC	0x20000000
#define BMI_ERR_INTR_EN_DISPATCH_RAM_ECC	0x10000000
#define BMI_NUM_OF_TASKS_MASK			0x3F000000
#define BMI_NUM_OF_EXTRA_TASKS_MASK		0x000F0000
#define BMI_NUM_OF_DMAS_MASK			0x00000F00
#define BMI_NUM_OF_EXTRA_DMAS_MASK		0x0000000F
#define BMI_FIFO_SIZE_MASK			0x000003FF
#define BMI_EXTRA_FIFO_SIZE_MASK		0x03FF0000
#define BMI_CFG2_DMAS_MASK			0x0000003F
#define BMI_CFG2_TASKS_MASK			0x0000003F

#define BMI_CFG2_TASKS_SHIFT		16
#define BMI_CFG2_DMAS_SHIFT		0
#define BMI_CFG1_FIFO_SIZE_SHIFT	16
#define BMI_NUM_OF_TASKS_SHIFT		24
#define BMI_EXTRA_NUM_OF_TASKS_SHIFT	16
#define BMI_NUM_OF_DMAS_SHIFT		8
#define BMI_EXTRA_NUM_OF_DMAS_SHIFT	0

#define BMI_FIFO_ALIGN			0x100

#define BMI_EXTRA_FIFO_SIZE_SHIFT	16

/* QMI defines */
#define QMI_CFG_ENQ_EN			0x80000000
#define QMI_CFG_DEQ_EN			0x40000000
#define QMI_CFG_EN_COUNTERS		0x10000000
#define QMI_CFG_DEQ_MASK		0x0000003F
#define QMI_CFG_ENQ_MASK		0x00003F00
#define QMI_CFG_ENQ_SHIFT		8

#define QMI_ERR_INTR_EN_DOUBLE_ECC	0x80000000
#define QMI_ERR_INTR_EN_DEQ_FROM_DEF	0x40000000
#define QMI_INTR_EN_SINGLE_ECC		0x80000000

#define QMI_TAPC_TAP			22

#define QMI_GS_HALT_NOT_BUSY		0x00000002

/* IRAM defines */
#define IRAM_IADD_AIE			0x80000000
#define IRAM_READY			0x80000000

/* Default values */
#define DEFAULT_CATASTROPHIC_ERR		0
#define DEFAULT_DMA_ERR				0
#define DEFAULT_AID_MODE			FMAN_DMA_AID_OUT_TNUM
#define DEFAULT_DMA_COMM_Q_LOW			0x2A
#define DEFAULT_DMA_COMM_Q_HIGH		0x3F
#define DEFAULT_CACHE_OVERRIDE			0
#define DEFAULT_DMA_CAM_NUM_OF_ENTRIES		64
#define DEFAULT_DMA_DBG_CNT_MODE		0
#define DEFAULT_DMA_SOS_EMERGENCY		0
#define DEFAULT_DMA_WATCHDOG			0
#define DEFAULT_DMA_EMERGENCY_SWITCH_COUNTER	0
#define DEFAULT_DISP_LIMIT			0
#define DEFAULT_PRS_DISP_TH			16
#define DEFAULT_PLCR_DISP_TH			16
#define DEFAULT_KG_DISP_TH			16
#define DEFAULT_BMI_DISP_TH			16
#define DEFAULT_QMI_ENQ_DISP_TH		16
#define DEFAULT_QMI_DEQ_DISP_TH		16
#define DEFAULT_FM_CTL1_DISP_TH		16
#define DEFAULT_FM_CTL2_DISP_TH		16

#define DFLT_AXI_DBG_NUM_OF_BEATS		1

#define DFLT_DMA_READ_INT_BUF_LOW(dma_thresh_max_buf)	\
	((dma_thresh_max_buf + 1) / 2)
#define DFLT_DMA_READ_INT_BUF_HIGH(dma_thresh_max_buf)	\
	((dma_thresh_max_buf + 1) * 3 / 4)
#define DFLT_DMA_WRITE_INT_BUF_LOW(dma_thresh_max_buf)	\
	((dma_thresh_max_buf + 1) / 2)
#define DFLT_DMA_WRITE_INT_BUF_HIGH(dma_thresh_max_buf)\
	((dma_thresh_max_buf + 1) * 3 / 4)

#define DMA_COMM_Q_LOW_FMAN_V3		0x2A
#define DMA_COMM_Q_LOW_FMAN_V2(dma_thresh_max_commq)		\
	((dma_thresh_max_commq + 1) / 2)
#define DFLT_DMA_COMM_Q_LOW(major, dma_thresh_max_commq)	\
	((major == 6) ? DMA_COMM_Q_LOW_FMAN_V3 :		\
	DMA_COMM_Q_LOW_FMAN_V2(dma_thresh_max_commq))

#define DMA_COMM_Q_HIGH_FMAN_V3	0x3f
#define DMA_COMM_Q_HIGH_FMAN_V2(dma_thresh_max_commq)		\
	((dma_thresh_max_commq + 1) * 3 / 4)
#define DFLT_DMA_COMM_Q_HIGH(major, dma_thresh_max_commq)	\
	((major == 6) ? DMA_COMM_Q_HIGH_FMAN_V3 :		\
	DMA_COMM_Q_HIGH_FMAN_V2(dma_thresh_max_commq))

#define TOTAL_NUM_OF_TASKS_FMAN_V3L	59
#define TOTAL_NUM_OF_TASKS_FMAN_V3H	124
#define DFLT_TOTAL_NUM_OF_TASKS(major, minor, bmi_max_num_of_tasks)	\
	((major == 6) ? ((minor == 1 || minor == 4) ?			\
	TOTAL_NUM_OF_TASKS_FMAN_V3L : TOTAL_NUM_OF_TASKS_FMAN_V3H) :	\
	bmi_max_num_of_tasks)

#define DMA_CAM_NUM_OF_ENTRIES_FMAN_V3		64
#define DMA_CAM_NUM_OF_ENTRIES_FMAN_V2		32
#define DFLT_DMA_CAM_NUM_OF_ENTRIES(major)			\
	(major == 6 ? DMA_CAM_NUM_OF_ENTRIES_FMAN_V3 :		\
	DMA_CAM_NUM_OF_ENTRIES_FMAN_V2)

#define FM_TIMESTAMP_1_USEC_BIT             8

/* Defines used for enabling/disabling FMan interrupts */
#define ERR_INTR_EN_DMA         0x00010000
#define ERR_INTR_EN_FPM         0x80000000
#define ERR_INTR_EN_BMI         0x00800000
#define ERR_INTR_EN_QMI         0x00400000
#define ERR_INTR_EN_MURAM       0x00040000
#define ERR_INTR_EN_MAC0        0x00004000
#define ERR_INTR_EN_MAC1        0x00002000
#define ERR_INTR_EN_MAC2        0x00001000
#define ERR_INTR_EN_MAC3        0x00000800
#define ERR_INTR_EN_MAC4        0x00000400
#define ERR_INTR_EN_MAC5        0x00000200
#define ERR_INTR_EN_MAC6        0x00000100
#define ERR_INTR_EN_MAC7        0x00000080
#define ERR_INTR_EN_MAC8        0x00008000
#define ERR_INTR_EN_MAC9        0x00000040

#define INTR_EN_QMI             0x40000000
#define INTR_EN_MAC0            0x00080000
#define INTR_EN_MAC1            0x00040000
#define INTR_EN_MAC2            0x00020000
#define INTR_EN_MAC3            0x00010000
#define INTR_EN_MAC4            0x00000040
#define INTR_EN_MAC5            0x00000020
#define INTR_EN_MAC6            0x00000008
#define INTR_EN_MAC7            0x00000002
#define INTR_EN_MAC8            0x00200000
#define INTR_EN_MAC9            0x00100000
#define INTR_EN_REV0            0x00008000
#define INTR_EN_REV1            0x00004000
#define INTR_EN_REV2            0x00002000
#define INTR_EN_REV3            0x00001000
#define INTR_EN_TMR             0x01000000

enum fman_dma_aid_mode {
	FMAN_DMA_AID_OUT_PORT_ID = 0,		  /* 4 LSB of PORT_ID */
	FMAN_DMA_AID_OUT_TNUM			  /* 4 LSB of TNUM */
};

struct fman_iram_regs {
	u32 iadd;	/* FM IRAM instruction address register */
	u32 idata;	/* FM IRAM instruction data register */
	u32 itcfg;	/* FM IRAM timing config register */
	u32 iready;	/* FM IRAM ready register */
};

struct fman_fpm_regs {
	u32 fmfp_tnc;		/* FPM TNUM Control 0x00 */
	u32 fmfp_prc;		/* FPM Port_ID FmCtl Association 0x04 */
	u32 fmfp_brkc;		/* FPM Breakpoint Control 0x08 */
	u32 fmfp_mxd;		/* FPM Flush Control 0x0c */
	u32 fmfp_dist1;		/* FPM Dispatch Thresholds1 0x10 */
	u32 fmfp_dist2;		/* FPM Dispatch Thresholds2 0x14 */
	u32 fm_epi;		/* FM Error Pending Interrupts 0x18 */
	u32 fm_rie;		/* FM Error Interrupt Enable 0x1c */
	u32 fmfp_fcev[4];	/* FPM FMan-Controller Event 1-4 0x20-0x2f */
	u32 res0030[4];		/* res 0x30 - 0x3f */
	u32 fmfp_cee[4];	/* PM FMan-Controller Event 1-4 0x40-0x4f */
	u32 res0050[4];		/* res 0x50-0x5f */
	u32 fmfp_tsc1;		/* FPM TimeStamp Control1 0x60 */
	u32 fmfp_tsc2;		/* FPM TimeStamp Control2 0x64 */
	u32 fmfp_tsp;		/* FPM Time Stamp 0x68 */
	u32 fmfp_tsf;		/* FPM Time Stamp Fraction 0x6c */
	u32 fm_rcr;		/* FM Rams Control 0x70 */
	u32 fmfp_extc;		/* FPM External Requests Control 0x74 */
	u32 fmfp_ext1;		/* FPM External Requests Config1 0x78 */
	u32 fmfp_ext2;		/* FPM External Requests Config2 0x7c */
	u32 fmfp_drd[16];	/* FPM Data_Ram Data 0-15 0x80 - 0xbf */
	u32 fmfp_dra;		/* FPM Data Ram Access 0xc0 */
	u32 fm_ip_rev_1;	/* FM IP Block Revision 1 0xc4 */
	u32 fm_ip_rev_2;	/* FM IP Block Revision 2 0xc8 */
	u32 fm_rstc;		/* FM Reset Command 0xcc */
	u32 fm_cld;		/* FM Classifier Debug 0xd0 */
	u32 fm_npi;		/* FM Normal Pending Interrupts 0xd4 */
	u32 fmfp_exte;		/* FPM External Requests Enable 0xd8 */
	u32 fmfp_ee;		/* FPM Event&Mask 0xdc */
	u32 fmfp_cev[4];	/* FPM CPU Event 1-4 0xe0-0xef */
	u32 res00f0[4];		/* res 0xf0-0xff */
	u32 fmfp_ps[50];	/* FPM Port Status 0x100-0x1c7 */
	u32 res01c8[14];	/* res 0x1c8-0x1ff */
	u32 fmfp_clfabc;	/* FPM CLFABC 0x200 */
	u32 fmfp_clfcc;		/* FPM CLFCC 0x204 */
	u32 fmfp_clfaval;	/* FPM CLFAVAL 0x208 */
	u32 fmfp_clfbval;	/* FPM CLFBVAL 0x20c */
	u32 fmfp_clfcval;	/* FPM CLFCVAL 0x210 */
	u32 fmfp_clfamsk;	/* FPM CLFAMSK 0x214 */
	u32 fmfp_clfbmsk;	/* FPM CLFBMSK 0x218 */
	u32 fmfp_clfcmsk;	/* FPM CLFCMSK 0x21c */
	u32 fmfp_clfamc;	/* FPM CLFAMC 0x220 */
	u32 fmfp_clfbmc;	/* FPM CLFBMC 0x224 */
	u32 fmfp_clfcmc;	/* FPM CLFCMC 0x228 */
	u32 fmfp_decceh;	/* FPM DECCEH 0x22c */
	u32 res0230[116];	/* res 0x230 - 0x3ff */
	u32 fmfp_ts[128];	/* 0x400: FPM Task Status 0x400 - 0x5ff */
	u32 res0600[0x400 - 384];
};

struct fman_bmi_regs {
	u32 fmbm_init;		/* BMI Initialization 0x00 */
	u32 fmbm_cfg1;		/* BMI Configuration 1 0x04 */
	u32 fmbm_cfg2;		/* BMI Configuration 2 0x08 */
	u32 res000c[5];		/* 0x0c - 0x1f */
	u32 fmbm_ievr;		/* Interrupt Event Register 0x20 */
	u32 fmbm_ier;		/* Interrupt Enable Register 0x24 */
	u32 fmbm_ifr;		/* Interrupt Force Register 0x28 */
	u32 res002c[5];		/* 0x2c - 0x3f */
	u32 fmbm_arb[8];	/* BMI Arbitration 0x40 - 0x5f */
	u32 res0060[12];	/* 0x60 - 0x8f */
	u32 fmbm_dtc[3];	/* Debug Trap Counter 0x90 - 0x9b */
	u32 res009c;		/* 0x9c */
	u32 fmbm_dcv[3][4];	/* Debug Compare val 0xa0-0xcf */
	u32 fmbm_dcm[3][4];	/* Debug Compare Mask 0xd0-0xff */
	u32 fmbm_gde;		/* BMI Global Debug Enable 0x100 */
	u32 fmbm_pp[63];	/* BMI Port Parameters 0x104 - 0x1ff */
	u32 res0200;		/* 0x200 */
	u32 fmbm_pfs[63];	/* BMI Port FIFO Size 0x204 - 0x2ff */
	u32 res0300;		/* 0x300 */
	u32 fmbm_spliodn[63];	/* Port Partition ID 0x304 - 0x3ff */
};

struct fman_qmi_regs {
	u32 fmqm_gc;		/* General Configuration Register 0x00 */
	u32 res0004;		/* 0x04 */
	u32 fmqm_eie;		/* Error Interrupt Event Register 0x08 */
	u32 fmqm_eien;		/* Error Interrupt Enable Register 0x0c */
	u32 fmqm_eif;		/* Error Interrupt Force Register 0x10 */
	u32 fmqm_ie;		/* Interrupt Event Register 0x14 */
	u32 fmqm_ien;		/* Interrupt Enable Register 0x18 */
	u32 fmqm_if;		/* Interrupt Force Register 0x1c */
	u32 fmqm_gs;		/* Global Status Register 0x20 */
	u32 fmqm_ts;		/* Task Status Register 0x24 */
	u32 fmqm_etfc;		/* Enqueue Total Frame Counter 0x28 */
	u32 fmqm_dtfc;		/* Dequeue Total Frame Counter 0x2c */
	u32 fmqm_dc0;		/* Dequeue Counter 0 0x30 */
	u32 fmqm_dc1;		/* Dequeue Counter 1 0x34 */
	u32 fmqm_dc2;		/* Dequeue Counter 2 0x38 */
	u32 fmqm_dc3;		/* Dequeue Counter 3 0x3c */
	u32 fmqm_dfdc;		/* Dequeue FQID from Default Counter 0x40 */
	u32 fmqm_dfcc;		/* Dequeue FQID from Context Counter 0x44 */
	u32 fmqm_dffc;		/* Dequeue FQID from FD Counter 0x48 */
	u32 fmqm_dcc;		/* Dequeue Confirm Counter 0x4c */
	u32 res0050[7];		/* 0x50 - 0x6b */
	u32 fmqm_tapc;		/* Tnum Aging Period Control 0x6c */
	u32 fmqm_dmcvc;		/* Dequeue MAC Command Valid Counter 0x70 */
	u32 fmqm_difdcc;	/* Dequeue Invalid FD Command Counter 0x74 */
	u32 fmqm_da1v;		/* Dequeue A1 Valid Counter 0x78 */
	u32 res007c;		/* 0x7c */
	u32 fmqm_dtc;		/* 0x80 Debug Trap Counter 0x80 */
	u32 fmqm_efddd;		/* 0x84 Enqueue Frame desc Dynamic dbg 0x84 */
	u32 res0088[2];		/* 0x88 - 0x8f */
	struct {
		u32 fmqm_dtcfg1;	/* 0x90 dbg trap cfg 1 Register 0x00 */
		u32 fmqm_dtval1;	/* Debug Trap Value 1 Register 0x04 */
		u32 fmqm_dtm1;		/* Debug Trap Mask 1 Register 0x08 */
		u32 fmqm_dtc1;		/* Debug Trap Counter 1 Register 0x0c */
		u32 fmqm_dtcfg2;	/* dbg Trap cfg 2 Register 0x10 */
		u32 fmqm_dtval2;	/* Debug Trap Value 2 Register 0x14 */
		u32 fmqm_dtm2;		/* Debug Trap Mask 2 Register 0x18 */
		u32 res001c;		/* 0x1c */
	} dbg_traps[3];			/* 0x90 - 0xef */
	u8 res00f0[0x400 - 0xf0];	/* 0xf0 - 0x3ff */
};

struct fman_dma_regs {
	u32 fmdmsr;	/* FM DMA status register 0x00 */
	u32 fmdmmr;	/* FM DMA mode register 0x04 */
	u32 fmdmtr;	/* FM DMA bus threshold register 0x08 */
	u32 fmdmhy;	/* FM DMA bus hysteresis register 0x0c */
	u32 fmdmsetr;	/* FM DMA SOS emergency Threshold Register 0x10 */
	u32 fmdmtah;	/* FM DMA transfer bus address high reg 0x14 */
	u32 fmdmtal;	/* FM DMA transfer bus address low reg 0x18 */
	u32 fmdmtcid;	/* FM DMA transfer bus communication ID reg 0x1c */
	u32 fmdmra;	/* FM DMA bus internal ram address register 0x20 */
	u32 fmdmrd;	/* FM DMA bus internal ram data register 0x24 */
	u32 fmdmwcr;	/* FM DMA CAM watchdog counter value 0x28 */
	u32 fmdmebcr;	/* FM DMA CAM base in MURAM register 0x2c */
	u32 fmdmccqdr;	/* FM DMA CAM and CMD Queue Debug reg 0x30 */
	u32 fmdmccqvr1;	/* FM DMA CAM and CMD Queue Value reg #1 0x34 */
	u32 fmdmccqvr2;	/* FM DMA CAM and CMD Queue Value reg #2 0x38 */
	u32 fmdmcqvr3;	/* FM DMA CMD Queue Value register #3 0x3c */
	u32 fmdmcqvr4;	/* FM DMA CMD Queue Value register #4 0x40 */
	u32 fmdmcqvr5;	/* FM DMA CMD Queue Value register #5 0x44 */
	u32 fmdmsefrc;	/* FM DMA Semaphore Entry Full Reject Cntr 0x48 */
	u32 fmdmsqfrc;	/* FM DMA Semaphore Queue Full Reject Cntr 0x4c */
	u32 fmdmssrc;	/* FM DMA Semaphore SYNC Reject Counter 0x50 */
	u32 fmdmdcr;	/* FM DMA Debug Counter 0x54 */
	u32 fmdmemsr;	/* FM DMA Emergency Smoother Register 0x58 */
	u32 res005c;	/* 0x5c */
	u32 fmdmplr[FMAN_LIODN_TBL / 2];	/* DMA LIODN regs 0x60-0xdf */
	u32 res00e0[0x400 - 56];
};

struct fman_rg {
	struct fman_fpm_regs __iomem *fpm_rg;
	struct fman_dma_regs __iomem *dma_rg;
	struct fman_bmi_regs __iomem *bmi_rg;
	struct fman_qmi_regs __iomem *qmi_rg;
};

struct fman_state_struct {
	u8 fm_id;
	u16 fm_clk_freq;
	struct fman_rev_info rev_info;
	bool enabled_time_stamp;
	u8 count1_micro_bit;
	u8 total_num_of_tasks;
	u8 accumulated_num_of_tasks;
	u32 accumulated_fifo_size;
	u8 accumulated_num_of_open_dmas;
	u8 accumulated_num_of_deq_tnums;
	bool low_end_restriction;
	u32 exceptions;
	u32 extra_fifo_pool_size;
	u8 extra_tasks_pool_size;
	u8 extra_open_dmas_pool_size;
	u16 port_mfl[MAX_NUM_OF_MACS];
	u16 mac_mfl[MAX_NUM_OF_MACS];

	/* SOC specific */
	u32 fm_iram_size;
	/* DMA */
	u32 dma_thresh_max_commq;
	u32 dma_thresh_max_buf;
	u32 max_num_of_open_dmas;
	/* QMI */
	u32 qmi_max_num_of_tnums;
	u32 qmi_def_tnums_thresh;
	/* BMI */
	u32 bmi_max_num_of_tasks;
	u32 bmi_max_fifo_size;
	/* General */
	u32 fm_port_num_of_cg;
	u32 num_of_rx_ports;
	u32 total_fifo_size;

	u32 qman_channel_base;
	u32 num_of_qman_channels;

	struct resource *res;
};

struct fman_cfg {
	u8 disp_limit_tsh;
	u8 prs_disp_tsh;
	u8 plcr_disp_tsh;
	u8 kg_disp_tsh;
	u8 bmi_disp_tsh;
	u8 qmi_enq_disp_tsh;
	u8 qmi_deq_disp_tsh;
	u8 fm_ctl1_disp_tsh;
	u8 fm_ctl2_disp_tsh;
	int dma_cache_override;
	enum fman_dma_aid_mode dma_aid_mode;
	bool dma_aid_override;
	u32 dma_axi_dbg_num_of_beats;
	u32 dma_cam_num_of_entries;
	u32 dma_watchdog;
	u8 dma_comm_qtsh_asrt_emer;
	u32 dma_write_buf_tsh_asrt_emer;
	u32 dma_read_buf_tsh_asrt_emer;
	u8 dma_comm_qtsh_clr_emer;
	u32 dma_write_buf_tsh_clr_emer;
	u32 dma_read_buf_tsh_clr_emer;
	u32 dma_sos_emergency;
	int dma_dbg_cnt_mode;
	bool dma_stop_on_bus_error;
	bool dma_en_emergency;
	u32 dma_emergency_bus_select;
	int dma_emergency_level;
	bool dma_en_emergency_smoother;
	u32 dma_emergency_switch_counter;
	bool halt_on_external_activ;
	bool halt_on_unrecov_ecc_err;
	int catastrophic_err;
	int dma_err;
	bool en_muram_test_mode;
	bool en_iram_test_mode;
	bool external_ecc_rams_enable;
	u16 tnum_aging_period;
	u32 exceptions;
	u16 clk_freq;
	bool pedantic_dma;
	u32 cam_base_addr;
	u32 fifo_base_addr;
	u32 total_fifo_size;
	u32 total_num_of_tasks;
	bool qmi_deq_option_support;
	u32 qmi_def_tnums_thresh;
};

struct fman_dts_params {
	void __iomem *base_addr;		/* FMan virtual address */
#ifndef __rtems__
	struct resource *res;			/* FMan memory resource */
#endif /* __rtems__ */
	u8 id;					/* FMan ID */

	int err_irq;				/* FMan Error IRQ */

	u16 clk_freq;				/* FMan clock freq (In Mhz) */

	u32 qman_channel_base;			/* QMan channels base */
	u32 num_of_qman_channels;		/* Number of QMan channels */

	phys_addr_t muram_phy_base_addr;	/* MURAM physical address */
	resource_size_t muram_size;		/* MURAM size */
};

struct fman {
	struct device *dev;
	void __iomem *base_addr;
	struct fman_intr_src intr_mng[FMAN_EV_CNT];

	struct fman_fpm_regs __iomem *fpm_regs;
	struct fman_bmi_regs __iomem *bmi_regs;
	struct fman_qmi_regs __iomem *qmi_regs;
	struct fman_dma_regs __iomem *dma_regs;
	fman_exceptions_cb *exception_cb;
	fman_bus_error_cb *bus_error_cb;
	/* Spinlock for FMan use */
	spinlock_t spinlock;
	struct fman_state_struct *state;

	struct fman_cfg *cfg;
	struct muram_info *muram;
	/* cam section in muram */
	int cam_offset;
	size_t cam_size;
	/* Fifo in MURAM */
	int fifo_offset;
	size_t fifo_size;
	bool reset_on_init;

	u32 liodn_base[64];
	u32 liodn_offset[64];

	struct fman_dts_params dts_params;
};

static void fman_exceptions(struct fman *fman, enum fman_exceptions exception)
{
	pr_debug("FMan[%d] exception %d\n",
		 fman->state->fm_id, exception);
}

static void fman_bus_error(struct fman *fman, u8 __maybe_unused port_id,
			   u64 __maybe_unused addr, u8 __maybe_unused tnum,
			   u16 __maybe_unused liodn)
{
	pr_debug("FMan[%d] bus error: port_id[%d]\n",
		 fman->state->fm_id, port_id);
}

static inline void call_mac_isr(struct fman *fman, u8 id)
{
	if (fman->intr_mng[id].isr_cb)
		fman->intr_mng[id].isr_cb(fman->intr_mng[id].src_handle);
}

static inline u8 hw_port_id_to_sw_port_id(u8 major, u8 hw_port_id)
{
	u8 sw_port_id = 0;

	if (hw_port_id >= BASE_TX_PORTID) {
		sw_port_id = hw_port_id - BASE_TX_PORTID;
	} else if (hw_port_id >= BASE_RX_PORTID) {
		sw_port_id = hw_port_id - BASE_RX_PORTID;
	} else {
		sw_port_id = 0;
		WARN_ON(false);
	}

	return sw_port_id;
}

static void set_port_order_restoration(struct fman_fpm_regs __iomem *fpm_rg,
				       u8 port_id)
{
	u32 tmp = 0;

	tmp = (u32)(port_id << FPM_PORT_FM_CTL_PORTID_SHIFT);

	tmp |= (FPM_PRT_FM_CTL2 | FPM_PRT_FM_CTL1);

	/* order restoration */
	if (port_id % 2)
		tmp |= (FPM_PRT_FM_CTL1 << FPM_PRC_ORA_FM_CTL_SEL_SHIFT);
	else
		tmp |= (FPM_PRT_FM_CTL2 << FPM_PRC_ORA_FM_CTL_SEL_SHIFT);

	iowrite32be(tmp, &fpm_rg->fmfp_prc);
}

static void set_port_liodn(struct fman_rg *fman_rg, u8 port_id,
			   u32 liodn_base, u32 liodn_ofst)
{
	u32 tmp;

	/* set LIODN base for this port */
	tmp = ioread32be(&fman_rg->dma_rg->fmdmplr[port_id / 2]);
	if (port_id % 2) {
		tmp &= ~DMA_LIODN_BASE_MASK;
		tmp |= liodn_base;
	} else {
		tmp &= ~(DMA_LIODN_BASE_MASK << DMA_LIODN_SHIFT);
		tmp |= liodn_base << DMA_LIODN_SHIFT;
	}
	iowrite32be(tmp, &fman_rg->dma_rg->fmdmplr[port_id / 2]);
	iowrite32be(liodn_ofst, &fman_rg->bmi_rg->fmbm_spliodn[port_id - 1]);
}

static void enable_rams_ecc(struct fman_fpm_regs __iomem *fpm_rg)
{
	u32 tmp;

	tmp = ioread32be(&fpm_rg->fm_rcr);
	if (tmp & FPM_RAM_RAMS_ECC_EN_SRC_SEL)
		iowrite32be(tmp | FPM_RAM_IRAM_ECC_EN, &fpm_rg->fm_rcr);
	else
		iowrite32be(tmp | FPM_RAM_RAMS_ECC_EN |
			    FPM_RAM_IRAM_ECC_EN, &fpm_rg->fm_rcr);
}

static void disable_rams_ecc(struct fman_fpm_regs __iomem *fpm_rg)
{
	u32 tmp;

	tmp = ioread32be(&fpm_rg->fm_rcr);
	if (tmp & FPM_RAM_RAMS_ECC_EN_SRC_SEL)
		iowrite32be(tmp & ~FPM_RAM_IRAM_ECC_EN, &fpm_rg->fm_rcr);
	else
		iowrite32be(tmp & ~(FPM_RAM_RAMS_ECC_EN | FPM_RAM_IRAM_ECC_EN),
			    &fpm_rg->fm_rcr);
}

static void fman_defconfig(struct fman_cfg *cfg)
{
	memset(cfg, 0, sizeof(struct fman_cfg));

	cfg->catastrophic_err = DEFAULT_CATASTROPHIC_ERR;
	cfg->dma_err = DEFAULT_DMA_ERR;
	cfg->halt_on_external_activ = false;
	cfg->halt_on_unrecov_ecc_err = false;
	cfg->en_iram_test_mode = false;
	cfg->en_muram_test_mode = false;
	cfg->external_ecc_rams_enable = false;
	cfg->dma_aid_override = false;
	cfg->dma_aid_mode = DEFAULT_AID_MODE;
	cfg->dma_comm_qtsh_clr_emer = DEFAULT_DMA_COMM_Q_LOW;
	cfg->dma_comm_qtsh_asrt_emer = DEFAULT_DMA_COMM_Q_HIGH;
	cfg->dma_cache_override = DEFAULT_CACHE_OVERRIDE;
	cfg->dma_cam_num_of_entries = DEFAULT_DMA_CAM_NUM_OF_ENTRIES;
	cfg->dma_dbg_cnt_mode = DEFAULT_DMA_DBG_CNT_MODE;
	cfg->dma_en_emergency = false;
	cfg->dma_sos_emergency = DEFAULT_DMA_SOS_EMERGENCY;
	cfg->dma_watchdog = DEFAULT_DMA_WATCHDOG;
	cfg->dma_en_emergency_smoother = false;
	cfg->dma_emergency_switch_counter =
	    DEFAULT_DMA_EMERGENCY_SWITCH_COUNTER;
	cfg->disp_limit_tsh = DEFAULT_DISP_LIMIT;
	cfg->prs_disp_tsh = DEFAULT_PRS_DISP_TH;
	cfg->plcr_disp_tsh = DEFAULT_PLCR_DISP_TH;
	cfg->kg_disp_tsh = DEFAULT_KG_DISP_TH;
	cfg->bmi_disp_tsh = DEFAULT_BMI_DISP_TH;
	cfg->qmi_enq_disp_tsh = DEFAULT_QMI_ENQ_DISP_TH;
	cfg->qmi_deq_disp_tsh = DEFAULT_QMI_DEQ_DISP_TH;
	cfg->fm_ctl1_disp_tsh = DEFAULT_FM_CTL1_DISP_TH;
	cfg->fm_ctl2_disp_tsh = DEFAULT_FM_CTL2_DISP_TH;

	cfg->pedantic_dma = false;
	cfg->tnum_aging_period = 0;
	cfg->dma_stop_on_bus_error = false;
	cfg->qmi_deq_option_support = false;
}

static int dma_init(struct fman *fman)
{
	struct fman_dma_regs __iomem *dma_rg = fman->dma_regs;
	struct fman_cfg *cfg = fman->cfg;
	u32 tmp_reg;

	/* Init DMA Registers */

	/* clear status reg events */
	tmp_reg = (DMA_STATUS_BUS_ERR | DMA_STATUS_READ_ECC |
		   DMA_STATUS_SYSTEM_WRITE_ECC | DMA_STATUS_FM_WRITE_ECC);
	iowrite32be(ioread32be(&dma_rg->fmdmsr) | tmp_reg, &dma_rg->fmdmsr);

	/* configure mode register */
	tmp_reg = 0;
	tmp_reg |= cfg->dma_cache_override << DMA_MODE_CACHE_OR_SHIFT;
	if (cfg->dma_aid_override)
		tmp_reg |= DMA_MODE_AID_OR;
	if (cfg->exceptions & EX_DMA_BUS_ERROR)
		tmp_reg |= DMA_MODE_BER;
	if ((cfg->exceptions & EX_DMA_SYSTEM_WRITE_ECC) |
	    (cfg->exceptions & EX_DMA_READ_ECC) |
	    (cfg->exceptions & EX_DMA_FM_WRITE_ECC))
		tmp_reg |= DMA_MODE_ECC;
	if (cfg->dma_stop_on_bus_error)
		tmp_reg |= DMA_MODE_SBER;
	if (cfg->dma_axi_dbg_num_of_beats)
		tmp_reg |= (DMA_MODE_AXI_DBG_MASK &
			((cfg->dma_axi_dbg_num_of_beats - 1)
			<< DMA_MODE_AXI_DBG_SHIFT));

	if (cfg->dma_en_emergency) {
		tmp_reg |= cfg->dma_emergency_bus_select;
		tmp_reg |= cfg->dma_emergency_level << DMA_MODE_EMER_LVL_SHIFT;
		if (cfg->dma_en_emergency_smoother)
			iowrite32be(cfg->dma_emergency_switch_counter,
				    &dma_rg->fmdmemsr);
	}
	tmp_reg |= (((cfg->dma_cam_num_of_entries / DMA_CAM_UNITS) - 1) &
		DMA_MODE_CEN_MASK) << DMA_MODE_CEN_SHIFT;
	tmp_reg |= DMA_MODE_SECURE_PROT;
	tmp_reg |= cfg->dma_dbg_cnt_mode << DMA_MODE_DBG_SHIFT;
	tmp_reg |= cfg->dma_aid_mode << DMA_MODE_AID_MODE_SHIFT;

	if (cfg->pedantic_dma)
		tmp_reg |= DMA_MODE_EMER_READ;

	iowrite32be(tmp_reg, &dma_rg->fmdmmr);

	/* configure thresholds register */
	tmp_reg = ((u32)cfg->dma_comm_qtsh_asrt_emer <<
		DMA_THRESH_COMMQ_SHIFT);
	tmp_reg |= (cfg->dma_read_buf_tsh_asrt_emer &
		DMA_THRESH_READ_INT_BUF_MASK) << DMA_THRESH_READ_INT_BUF_SHIFT;
	tmp_reg |= cfg->dma_write_buf_tsh_asrt_emer &
		DMA_THRESH_WRITE_INT_BUF_MASK;

	iowrite32be(tmp_reg, &dma_rg->fmdmtr);

	/* configure hysteresis register */
	tmp_reg = ((u32)cfg->dma_comm_qtsh_clr_emer <<
		DMA_THRESH_COMMQ_SHIFT);
	tmp_reg |= (cfg->dma_read_buf_tsh_clr_emer &
		DMA_THRESH_READ_INT_BUF_MASK) << DMA_THRESH_READ_INT_BUF_SHIFT;
	tmp_reg |= cfg->dma_write_buf_tsh_clr_emer &
		DMA_THRESH_WRITE_INT_BUF_MASK;

	iowrite32be(tmp_reg, &dma_rg->fmdmhy);

	/* configure emergency threshold */
	iowrite32be(cfg->dma_sos_emergency, &dma_rg->fmdmsetr);

	/* configure Watchdog */
	iowrite32be((cfg->dma_watchdog * cfg->clk_freq), &dma_rg->fmdmwcr);

	iowrite32be(cfg->cam_base_addr, &dma_rg->fmdmebcr);

	/* Allocate MURAM for CAM */
	fman->cam_size =
		(u32)(fman->cfg->dma_cam_num_of_entries * DMA_CAM_SIZEOF_ENTRY);
	fman->cam_offset = fman_muram_alloc(fman->muram, fman->cam_size);
	if (IS_ERR_VALUE(fman->cam_offset)) {
		pr_err("MURAM alloc for DMA CAM failed\n");
		return -ENOMEM;
	}

	if (fman->state->rev_info.major == 2) {
		u32 __iomem *cam_base_addr;

		fman_muram_free_mem(fman->muram, fman->cam_offset,
				    fman->cam_size);

		fman->cam_size = fman->cfg->dma_cam_num_of_entries * 72 + 128;
		fman->cam_offset = fman_muram_alloc(fman->muram,
						    fman->cam_size);
		if (IS_ERR_VALUE(fman->cam_offset)) {
			pr_err("MURAM alloc for DMA CAM failed\n");
			return -ENOMEM;
		}

		if (fman->cfg->dma_cam_num_of_entries % 8 ||
		    fman->cfg->dma_cam_num_of_entries > 32) {
			pr_err("wrong dma_cam_num_of_entries\n");
			return -EINVAL;
		}

		cam_base_addr = (u32 __iomem *)
			fman_muram_offset_to_vbase(fman->muram,
						   fman->cam_offset);
		out_be32(cam_base_addr,
			 ~((1 << (32 - fman->cfg->dma_cam_num_of_entries)) -
			 1));
	}

	fman->cfg->cam_base_addr = fman->cam_offset;

	return 0;
}

static void fpm_init(struct fman_fpm_regs __iomem *fpm_rg, struct fman_cfg *cfg)
{
	u32 tmp_reg;
	int i;

	/* Init FPM Registers */

	tmp_reg = (u32)(cfg->disp_limit_tsh << FPM_DISP_LIMIT_SHIFT);
	iowrite32be(tmp_reg, &fpm_rg->fmfp_mxd);

	tmp_reg = (((u32)cfg->prs_disp_tsh << FPM_THR1_PRS_SHIFT) |
		   ((u32)cfg->kg_disp_tsh << FPM_THR1_KG_SHIFT) |
		   ((u32)cfg->plcr_disp_tsh << FPM_THR1_PLCR_SHIFT) |
		   ((u32)cfg->bmi_disp_tsh << FPM_THR1_BMI_SHIFT));
	iowrite32be(tmp_reg, &fpm_rg->fmfp_dist1);

	tmp_reg =
		(((u32)cfg->qmi_enq_disp_tsh << FPM_THR2_QMI_ENQ_SHIFT) |
		 ((u32)cfg->qmi_deq_disp_tsh << FPM_THR2_QMI_DEQ_SHIFT) |
		 ((u32)cfg->fm_ctl1_disp_tsh << FPM_THR2_FM_CTL1_SHIFT) |
		 ((u32)cfg->fm_ctl2_disp_tsh << FPM_THR2_FM_CTL2_SHIFT));
	iowrite32be(tmp_reg, &fpm_rg->fmfp_dist2);

	/* define exceptions and error behavior */
	tmp_reg = 0;
	/* Clear events */
	tmp_reg |= (FPM_EV_MASK_STALL | FPM_EV_MASK_DOUBLE_ECC |
		    FPM_EV_MASK_SINGLE_ECC);
	/* enable interrupts */
	if (cfg->exceptions & EX_FPM_STALL_ON_TASKS)
		tmp_reg |= FPM_EV_MASK_STALL_EN;
	if (cfg->exceptions & EX_FPM_SINGLE_ECC)
		tmp_reg |= FPM_EV_MASK_SINGLE_ECC_EN;
	if (cfg->exceptions & EX_FPM_DOUBLE_ECC)
		tmp_reg |= FPM_EV_MASK_DOUBLE_ECC_EN;
	tmp_reg |= (cfg->catastrophic_err << FPM_EV_MASK_CAT_ERR_SHIFT);
	tmp_reg |= (cfg->dma_err << FPM_EV_MASK_DMA_ERR_SHIFT);
	if (!cfg->halt_on_external_activ)
		tmp_reg |= FPM_EV_MASK_EXTERNAL_HALT;
	if (!cfg->halt_on_unrecov_ecc_err)
		tmp_reg |= FPM_EV_MASK_ECC_ERR_HALT;
	iowrite32be(tmp_reg, &fpm_rg->fmfp_ee);

	/* clear all fmCtls event registers */
	for (i = 0; i < FM_NUM_OF_FMAN_CTRL_EVENT_REGS; i++)
		iowrite32be(0xFFFFFFFF, &fpm_rg->fmfp_cev[i]);

	/* RAM ECC -  enable and clear events */
	/* first we need to clear all parser memory,
	 * as it is uninitialized and may cause ECC errors
	 */
	/* event bits */
	tmp_reg = (FPM_RAM_MURAM_ECC | FPM_RAM_IRAM_ECC);
	/* Rams enable not effected by RCR bit,
	 * but by a COP configuration
	 */
	if (cfg->external_ecc_rams_enable)
		tmp_reg |= FPM_RAM_RAMS_ECC_EN_SRC_SEL;

	/* enable test mode */
	if (cfg->en_muram_test_mode)
		tmp_reg |= FPM_RAM_MURAM_TEST_ECC;
	if (cfg->en_iram_test_mode)
		tmp_reg |= FPM_RAM_IRAM_TEST_ECC;
	iowrite32be(tmp_reg, &fpm_rg->fm_rcr);

	tmp_reg = 0;
	if (cfg->exceptions & EX_IRAM_ECC) {
		tmp_reg |= FPM_IRAM_ECC_ERR_EX_EN;
		enable_rams_ecc(fpm_rg);
	}
	if (cfg->exceptions & EX_MURAM_ECC) {
		tmp_reg |= FPM_MURAM_ECC_ERR_EX_EN;
		enable_rams_ecc(fpm_rg);
	}
	iowrite32be(tmp_reg, &fpm_rg->fm_rie);
}

static void bmi_init(struct fman_bmi_regs __iomem *bmi_rg,
		     struct fman_cfg *cfg)
{
	u32 tmp_reg;

	/* Init BMI Registers */

	/* define common resources */
	tmp_reg = cfg->fifo_base_addr;
	tmp_reg = tmp_reg / BMI_FIFO_ALIGN;

	tmp_reg |= ((cfg->total_fifo_size / FMAN_BMI_FIFO_UNITS - 1) <<
		    BMI_CFG1_FIFO_SIZE_SHIFT);
	iowrite32be(tmp_reg, &bmi_rg->fmbm_cfg1);

	tmp_reg = ((cfg->total_num_of_tasks - 1) & BMI_CFG2_TASKS_MASK) <<
		   BMI_CFG2_TASKS_SHIFT;
	/* num of DMA's will be dynamically updated when each port is set */
	iowrite32be(tmp_reg, &bmi_rg->fmbm_cfg2);

	/* define unmaskable exceptions, enable and clear events */
	tmp_reg = 0;
	iowrite32be(BMI_ERR_INTR_EN_LIST_RAM_ECC |
		    BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC |
		    BMI_ERR_INTR_EN_STATISTICS_RAM_ECC |
		    BMI_ERR_INTR_EN_DISPATCH_RAM_ECC, &bmi_rg->fmbm_ievr);

	if (cfg->exceptions & EX_BMI_LIST_RAM_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_LIST_RAM_ECC;
	if (cfg->exceptions & EX_BMI_STORAGE_PROFILE_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC;
	if (cfg->exceptions & EX_BMI_STATISTICS_RAM_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_STATISTICS_RAM_ECC;
	if (cfg->exceptions & EX_BMI_DISPATCH_RAM_ECC)
		tmp_reg |= BMI_ERR_INTR_EN_DISPATCH_RAM_ECC;
	iowrite32be(tmp_reg, &bmi_rg->fmbm_ier);
}

static void qmi_init(struct fman_qmi_regs __iomem *qmi_rg,
		     struct fman_cfg *cfg)
{
	u32 tmp_reg;
	u16 period_in_fm_clocks;
	u8 remainder;

	/* Init QMI Registers */

	/* Clear error interrupt events */

	iowrite32be(QMI_ERR_INTR_EN_DOUBLE_ECC | QMI_ERR_INTR_EN_DEQ_FROM_DEF,
		    &qmi_rg->fmqm_eie);
	tmp_reg = 0;
	if (cfg->exceptions & EX_QMI_DEQ_FROM_UNKNOWN_PORTID)
		tmp_reg |= QMI_ERR_INTR_EN_DEQ_FROM_DEF;
	if (cfg->exceptions & EX_QMI_DOUBLE_ECC)
		tmp_reg |= QMI_ERR_INTR_EN_DOUBLE_ECC;
	/* enable events */
	iowrite32be(tmp_reg, &qmi_rg->fmqm_eien);

	if (cfg->tnum_aging_period) {
		/* tnum_aging_period is in units of usec, clk_freq in Mhz */
		period_in_fm_clocks = (u16)
			(cfg->tnum_aging_period * cfg->clk_freq);
		/* period_in_fm_clocks must be a 64 multiple */
		remainder = (u8)(period_in_fm_clocks % 64);
		if (remainder) {
			tmp_reg = (u32)((period_in_fm_clocks / 64) + 1);
		} else {
			tmp_reg = (u32)(period_in_fm_clocks / 64);
			if (!tmp_reg)
				tmp_reg = 1;
		}
		tmp_reg <<= QMI_TAPC_TAP;
		iowrite32be(tmp_reg, &qmi_rg->fmqm_tapc);
	}
	tmp_reg = 0;
	/* Clear interrupt events */
	iowrite32be(QMI_INTR_EN_SINGLE_ECC, &qmi_rg->fmqm_ie);
	if (cfg->exceptions & EX_QMI_SINGLE_ECC)
		tmp_reg |= QMI_INTR_EN_SINGLE_ECC;
	/* enable events */
	iowrite32be(tmp_reg, &qmi_rg->fmqm_ien);
}

static int enable(struct fman_rg *fman_rg, struct fman_cfg *cfg)
{
	u32 cfg_reg = 0;

	/* Enable all modules */

	/* clear&enable global counters	 - calculate reg and save for later,
	 * because it's the same reg for QMI enable
	 */
	cfg_reg = QMI_CFG_EN_COUNTERS;
	if (cfg->qmi_deq_option_support)
		cfg_reg |= (u32)(((cfg->qmi_def_tnums_thresh) << 8) |
				  cfg->qmi_def_tnums_thresh);

	iowrite32be(BMI_INIT_START, &fman_rg->bmi_rg->fmbm_init);
	iowrite32be(cfg_reg | QMI_CFG_ENQ_EN | QMI_CFG_DEQ_EN,
		    &fman_rg->qmi_rg->fmqm_gc);

	return 0;
}

static int set_exception(struct fman_rg *fman_rg,
			 enum fman_exceptions exception, bool enable)
{
	u32 tmp;

	switch (exception) {
	case FMAN_EX_DMA_BUS_ERROR:
		tmp = ioread32be(&fman_rg->dma_rg->fmdmmr);
		if (enable)
			tmp |= DMA_MODE_BER;
		else
			tmp &= ~DMA_MODE_BER;
		/* disable bus error */
		iowrite32be(tmp, &fman_rg->dma_rg->fmdmmr);
		break;
	case FMAN_EX_DMA_READ_ECC:
	case FMAN_EX_DMA_SYSTEM_WRITE_ECC:
	case FMAN_EX_DMA_FM_WRITE_ECC:
		tmp = ioread32be(&fman_rg->dma_rg->fmdmmr);
		if (enable)
			tmp |= DMA_MODE_ECC;
		else
			tmp &= ~DMA_MODE_ECC;
		iowrite32be(tmp, &fman_rg->dma_rg->fmdmmr);
		break;
	case FMAN_EX_FPM_STALL_ON_TASKS:
		tmp = ioread32be(&fman_rg->fpm_rg->fmfp_ee);
		if (enable)
			tmp |= FPM_EV_MASK_STALL_EN;
		else
			tmp &= ~FPM_EV_MASK_STALL_EN;
		iowrite32be(tmp, &fman_rg->fpm_rg->fmfp_ee);
		break;
	case FMAN_EX_FPM_SINGLE_ECC:
		tmp = ioread32be(&fman_rg->fpm_rg->fmfp_ee);
		if (enable)
			tmp |= FPM_EV_MASK_SINGLE_ECC_EN;
		else
			tmp &= ~FPM_EV_MASK_SINGLE_ECC_EN;
		iowrite32be(tmp, &fman_rg->fpm_rg->fmfp_ee);
		break;
	case FMAN_EX_FPM_DOUBLE_ECC:
		tmp = ioread32be(&fman_rg->fpm_rg->fmfp_ee);
		if (enable)
			tmp |= FPM_EV_MASK_DOUBLE_ECC_EN;
		else
			tmp &= ~FPM_EV_MASK_DOUBLE_ECC_EN;
		iowrite32be(tmp, &fman_rg->fpm_rg->fmfp_ee);
		break;
	case FMAN_EX_QMI_SINGLE_ECC:
		tmp = ioread32be(&fman_rg->qmi_rg->fmqm_ien);
		if (enable)
			tmp |= QMI_INTR_EN_SINGLE_ECC;
		else
			tmp &= ~QMI_INTR_EN_SINGLE_ECC;
		iowrite32be(tmp, &fman_rg->qmi_rg->fmqm_ien);
		break;
	case FMAN_EX_QMI_DOUBLE_ECC:
		tmp = ioread32be(&fman_rg->qmi_rg->fmqm_eien);
		if (enable)
			tmp |= QMI_ERR_INTR_EN_DOUBLE_ECC;
		else
			tmp &= ~QMI_ERR_INTR_EN_DOUBLE_ECC;
		iowrite32be(tmp, &fman_rg->qmi_rg->fmqm_eien);
		break;
	case FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID:
		tmp = ioread32be(&fman_rg->qmi_rg->fmqm_eien);
		if (enable)
			tmp |= QMI_ERR_INTR_EN_DEQ_FROM_DEF;
		else
			tmp &= ~QMI_ERR_INTR_EN_DEQ_FROM_DEF;
		iowrite32be(tmp, &fman_rg->qmi_rg->fmqm_eien);
		break;
	case FMAN_EX_BMI_LIST_RAM_ECC:
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_LIST_RAM_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_LIST_RAM_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case FMAN_EX_BMI_STORAGE_PROFILE_ECC:
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case FMAN_EX_BMI_STATISTICS_RAM_ECC:
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_STATISTICS_RAM_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_STATISTICS_RAM_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case FMAN_EX_BMI_DISPATCH_RAM_ECC:
		tmp = ioread32be(&fman_rg->bmi_rg->fmbm_ier);
		if (enable)
			tmp |= BMI_ERR_INTR_EN_DISPATCH_RAM_ECC;
		else
			tmp &= ~BMI_ERR_INTR_EN_DISPATCH_RAM_ECC;
		iowrite32be(tmp, &fman_rg->bmi_rg->fmbm_ier);
		break;
	case FMAN_EX_IRAM_ECC:
		tmp = ioread32be(&fman_rg->fpm_rg->fm_rie);
		if (enable) {
			/* enable ECC if not enabled */
			enable_rams_ecc(fman_rg->fpm_rg);
			/* enable ECC interrupts */
			tmp |= FPM_IRAM_ECC_ERR_EX_EN;
		} else {
			/* ECC mechanism may be disabled,
			 * depending on driver status
			 */
			disable_rams_ecc(fman_rg->fpm_rg);
			tmp &= ~FPM_IRAM_ECC_ERR_EX_EN;
		}
		iowrite32be(tmp, &fman_rg->fpm_rg->fm_rie);
		break;
	case FMAN_EX_MURAM_ECC:
		tmp = ioread32be(&fman_rg->fpm_rg->fm_rie);
		if (enable) {
			/* enable ECC if not enabled */
			enable_rams_ecc(fman_rg->fpm_rg);
			/* enable ECC interrupts */
			tmp |= FPM_MURAM_ECC_ERR_EX_EN;
		} else {
			/* ECC mechanism may be disabled,
			 * depending on driver status
			 */
			disable_rams_ecc(fman_rg->fpm_rg);
			tmp &= ~FPM_MURAM_ECC_ERR_EX_EN;
		}
		iowrite32be(tmp, &fman_rg->fpm_rg->fm_rie);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void resume(struct fman_fpm_regs __iomem *fpm_rg)
{
	u32 tmp;

	tmp = ioread32be(&fpm_rg->fmfp_ee);
	/* clear tmp_reg event bits in order not to clear standing events */
	tmp &= ~(FPM_EV_MASK_DOUBLE_ECC |
		 FPM_EV_MASK_STALL | FPM_EV_MASK_SINGLE_ECC);
	tmp |= FPM_EV_MASK_RELEASE_FM;

	iowrite32be(tmp, &fpm_rg->fmfp_ee);
}

static int fill_soc_specific_params(struct fman_state_struct *state)
{
	u8 minor = state->rev_info.minor;
	/* P4080 - Major 2
	 * P2041/P3041/P5020/P5040 - Major 3
	 * Tx/Bx - Major 6
	 */
	switch (state->rev_info.major) {
	case 3:
		state->bmi_max_fifo_size	= 160 * 1024;
		state->fm_iram_size		= 64 * 1024;
		state->dma_thresh_max_commq	= 31;
		state->dma_thresh_max_buf	= 127;
		state->qmi_max_num_of_tnums	= 64;
		state->qmi_def_tnums_thresh	= 48;
		state->bmi_max_num_of_tasks	= 128;
		state->max_num_of_open_dmas	= 32;
		state->fm_port_num_of_cg	= 256;
		state->num_of_rx_ports	= 6;
		state->total_fifo_size	= 122 * 1024;
		break;

	case 2:
		state->bmi_max_fifo_size	= 160 * 1024;
		state->fm_iram_size		= 64 * 1024;
		state->dma_thresh_max_commq	= 31;
		state->dma_thresh_max_buf	= 127;
		state->qmi_max_num_of_tnums	= 64;
		state->qmi_def_tnums_thresh	= 48;
		state->bmi_max_num_of_tasks	= 128;
		state->max_num_of_open_dmas	= 32;
		state->fm_port_num_of_cg	= 256;
		state->num_of_rx_ports	= 5;
		state->total_fifo_size	= 100 * 1024;
		break;

	case 6:
		state->dma_thresh_max_commq	= 83;
		state->dma_thresh_max_buf	= 127;
		state->qmi_max_num_of_tnums	= 64;
		state->qmi_def_tnums_thresh	= 32;
		state->fm_port_num_of_cg	= 256;

		/* FManV3L */
		if (minor == 1 || minor == 4) {
			state->bmi_max_fifo_size	= 192 * 1024;
			state->bmi_max_num_of_tasks	= 64;
			state->max_num_of_open_dmas	= 32;
			state->num_of_rx_ports		= 5;
			if (minor == 1)
				state->fm_iram_size	= 32 * 1024;
			else
				state->fm_iram_size	= 64 * 1024;
			state->total_fifo_size		= 156 * 1024;
		}
		/* FManV3H */
		else if (minor == 0 || minor == 2 || minor == 3) {
			state->bmi_max_fifo_size	= 384 * 1024;
			state->fm_iram_size		= 64 * 1024;
			state->bmi_max_num_of_tasks	= 128;
			state->max_num_of_open_dmas	= 84;
			state->num_of_rx_ports		= 8;
			state->total_fifo_size		= 295 * 1024;
		} else {
			pr_err("Unsupported FManv3 version\n");
			return -EINVAL;
		}

		break;
	default:
		pr_err("Unsupported FMan version\n");
		return -EINVAL;
	}

	return 0;
}

static bool is_init_done(struct fman_cfg *cfg)
{
	/* Checks if FMan driver parameters were initialized */
	if (!cfg)
		return true;

	return false;
}

static void free_init_resources(struct fman *fman)
{
	if (fman->cam_offset)
		fman_muram_free_mem(fman->muram, fman->cam_offset,
				    fman->cam_size);
	if (fman->fifo_offset)
		fman_muram_free_mem(fman->muram, fman->fifo_offset,
				    fman->fifo_size);
}

static void bmi_err_event(struct fman *fman)
{
	u32 event, mask, force;
	struct fman_bmi_regs __iomem *bmi_rg = fman->bmi_regs;

	event = ioread32be(&bmi_rg->fmbm_ievr);
	mask = ioread32be(&bmi_rg->fmbm_ier);
	event &= mask;
	/* clear the forced events */
	force = ioread32be(&bmi_rg->fmbm_ifr);
	if (force & event)
		iowrite32be(force & ~event, &bmi_rg->fmbm_ifr);
	/* clear the acknowledged events */
	iowrite32be(event, &bmi_rg->fmbm_ievr);

	if (event & BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC)
		fman->exception_cb(fman, FMAN_EX_BMI_STORAGE_PROFILE_ECC);
	if (event & BMI_ERR_INTR_EN_LIST_RAM_ECC)
		fman->exception_cb(fman, FMAN_EX_BMI_LIST_RAM_ECC);
	if (event & BMI_ERR_INTR_EN_STATISTICS_RAM_ECC)
		fman->exception_cb(fman, FMAN_EX_BMI_STATISTICS_RAM_ECC);
	if (event & BMI_ERR_INTR_EN_DISPATCH_RAM_ECC)
		fman->exception_cb(fman, FMAN_EX_BMI_DISPATCH_RAM_ECC);
}

static void qmi_err_event(struct fman *fman)
{
	u32 event, mask, force;
	struct fman_qmi_regs __iomem *qmi_rg = fman->qmi_regs;

	event = ioread32be(&qmi_rg->fmqm_eie);
	mask = ioread32be(&qmi_rg->fmqm_eien);
	event &= mask;

	/* clear the forced events */
	force = ioread32be(&qmi_rg->fmqm_eif);
	if (force & event)
		iowrite32be(force & ~event, &qmi_rg->fmqm_eif);
	/* clear the acknowledged events */
	iowrite32be(event, &qmi_rg->fmqm_eie);

	if (event & QMI_ERR_INTR_EN_DOUBLE_ECC)
		fman->exception_cb(fman, FMAN_EX_QMI_DOUBLE_ECC);
	if (event & QMI_ERR_INTR_EN_DEQ_FROM_DEF)
		fman->exception_cb(fman, FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID);
}

static void dma_err_event(struct fman *fman)
{
	u32 status, mask, com_id;
	u8 tnum, port_id, relative_port_id;
	u16 liodn;
	struct fman_dma_regs __iomem *dma_rg = fman->dma_regs;

	status = ioread32be(&dma_rg->fmdmsr);
	mask = ioread32be(&dma_rg->fmdmmr);

	/* clear DMA_STATUS_BUS_ERR if mask has no DMA_MODE_BER */
	if ((mask & DMA_MODE_BER) != DMA_MODE_BER)
		status &= ~DMA_STATUS_BUS_ERR;

	/* clear relevant bits if mask has no DMA_MODE_ECC */
	if ((mask & DMA_MODE_ECC) != DMA_MODE_ECC)
		status &= ~(DMA_STATUS_FM_SPDAT_ECC |
			    DMA_STATUS_READ_ECC |
			    DMA_STATUS_SYSTEM_WRITE_ECC |
			    DMA_STATUS_FM_WRITE_ECC);

	/* clear set events */
	iowrite32be(status, &dma_rg->fmdmsr);

	if (status & DMA_STATUS_BUS_ERR) {
		u64 addr;

		addr = (u64)ioread32be(&dma_rg->fmdmtal);
		addr |= ((u64)(ioread32be(&dma_rg->fmdmtah)) << 32);

		com_id = ioread32be(&dma_rg->fmdmtcid);
		port_id = (u8)(((com_id & DMA_TRANSFER_PORTID_MASK) >>
			       DMA_TRANSFER_PORTID_SHIFT));
		relative_port_id =
		hw_port_id_to_sw_port_id(fman->state->rev_info.major, port_id);
		tnum = (u8)((com_id & DMA_TRANSFER_TNUM_MASK) >>
			    DMA_TRANSFER_TNUM_SHIFT);
		liodn = (u16)(com_id & DMA_TRANSFER_LIODN_MASK);
		fman->bus_error_cb(fman, relative_port_id, addr, tnum, liodn);
	}
	if (status & DMA_STATUS_FM_SPDAT_ECC)
		fman->exception_cb(fman, FMAN_EX_DMA_SINGLE_PORT_ECC);
	if (status & DMA_STATUS_READ_ECC)
		fman->exception_cb(fman, FMAN_EX_DMA_READ_ECC);
	if (status & DMA_STATUS_SYSTEM_WRITE_ECC)
		fman->exception_cb(fman, FMAN_EX_DMA_SYSTEM_WRITE_ECC);
	if (status & DMA_STATUS_FM_WRITE_ECC)
		fman->exception_cb(fman, FMAN_EX_DMA_FM_WRITE_ECC);
}

static void fpm_err_event(struct fman *fman)
{
	u32 event;
	struct fman_fpm_regs __iomem *fpm_rg = fman->fpm_regs;

	event = ioread32be(&fpm_rg->fmfp_ee);
	/* clear the all occurred events */
	iowrite32be(event, &fpm_rg->fmfp_ee);

	if ((event & FPM_EV_MASK_DOUBLE_ECC) &&
	    (event & FPM_EV_MASK_DOUBLE_ECC_EN))
		fman->exception_cb(fman, FMAN_EX_FPM_DOUBLE_ECC);
	if ((event & FPM_EV_MASK_STALL) && (event & FPM_EV_MASK_STALL_EN))
		fman->exception_cb(fman, FMAN_EX_FPM_STALL_ON_TASKS);
	if ((event & FPM_EV_MASK_SINGLE_ECC) &&
	    (event & FPM_EV_MASK_SINGLE_ECC_EN))
		fman->exception_cb(fman, FMAN_EX_FPM_SINGLE_ECC);
}

static void muram_err_intr(struct fman *fman)
{
	u32 event, mask;
	struct fman_fpm_regs __iomem *fpm_rg = fman->fpm_regs;

	event = ioread32be(&fpm_rg->fm_rcr);
	mask = ioread32be(&fpm_rg->fm_rie);

	/* clear MURAM event bit (do not clear IRAM event) */
	iowrite32be(event & ~FPM_RAM_IRAM_ECC, &fpm_rg->fm_rcr);

	if ((mask & FPM_MURAM_ECC_ERR_EX_EN) && (event & FPM_RAM_MURAM_ECC))
		fman->exception_cb(fman, FMAN_EX_MURAM_ECC);
}

static void qmi_event(struct fman *fman)
{
	u32 event, mask, force;
	struct fman_qmi_regs __iomem *qmi_rg = fman->qmi_regs;

	event = ioread32be(&qmi_rg->fmqm_ie);
	mask = ioread32be(&qmi_rg->fmqm_ien);
	event &= mask;
	/* clear the forced events */
	force = ioread32be(&qmi_rg->fmqm_if);
	if (force & event)
		iowrite32be(force & ~event, &qmi_rg->fmqm_if);
	/* clear the acknowledged events */
	iowrite32be(event, &qmi_rg->fmqm_ie);

	if (event & QMI_INTR_EN_SINGLE_ECC)
		fman->exception_cb(fman, FMAN_EX_QMI_SINGLE_ECC);
}

static void enable_time_stamp(struct fman *fman)
{
	struct fman_fpm_regs __iomem *fpm_rg = fman->fpm_regs;
	u16 fm_clk_freq = fman->state->fm_clk_freq;
	u32 tmp, intgr, ts_freq;
	u64 frac;

	ts_freq = (u32)(1 << fman->state->count1_micro_bit);
	/* configure timestamp so that bit 8 will count 1 microsecond
	 * Find effective count rate at TIMESTAMP least significant bits:
	 * Effective_Count_Rate = 1MHz x 2^8 = 256MHz
	 * Find frequency ratio between effective count rate and the clock:
	 * Effective_Count_Rate / CLK e.g. for 600 MHz clock:
	 * 256/600 = 0.4266666...
	 */

	intgr = ts_freq / fm_clk_freq;
	/* we multiply by 2^16 to keep the fraction of the division
	 * we do not div back, since we write this value as a fraction
	 * see spec
	 */

	frac = ((ts_freq << 16) - (intgr << 16) * fm_clk_freq) / fm_clk_freq;
	/* we check remainder of the division in order to round up if not int */
	if (((ts_freq << 16) - (intgr << 16) * fm_clk_freq) % fm_clk_freq)
		frac++;

	tmp = (intgr << FPM_TS_INT_SHIFT) | (u16)frac;
	iowrite32be(tmp, &fpm_rg->fmfp_tsc2);

	/* enable timestamp with original clock */
	iowrite32be(FPM_TS_CTL_EN, &fpm_rg->fmfp_tsc1);
	fman->state->enabled_time_stamp = true;
}

static int clear_iram(struct fman *fman)
{
	struct fman_iram_regs __iomem *iram;
	int i;

	iram = (struct fman_iram_regs __iomem *)(fman->base_addr + IMEM_OFFSET);

	/* Enable the auto-increment */
	out_be32(&iram->iadd, IRAM_IADD_AIE);
	while (in_be32(&iram->iadd) != IRAM_IADD_AIE)
		;

	for (i = 0; i < (fman->state->fm_iram_size / 4); i++)
		out_be32(&iram->idata, 0xffffffff);

	out_be32(&iram->iadd, fman->state->fm_iram_size - 4);
	/* Memory barrier */
	mb();
	while (in_be32(&iram->idata) != 0xffffffff)
		;

	return 0;
}

static u32 get_exception_flag(enum fman_exceptions exception)
{
	u32 bit_mask;

	switch (exception) {
	case FMAN_EX_DMA_BUS_ERROR:
		bit_mask = EX_DMA_BUS_ERROR;
		break;
	case FMAN_EX_DMA_SINGLE_PORT_ECC:
		bit_mask = EX_DMA_SINGLE_PORT_ECC;
		break;
	case FMAN_EX_DMA_READ_ECC:
		bit_mask = EX_DMA_READ_ECC;
		break;
	case FMAN_EX_DMA_SYSTEM_WRITE_ECC:
		bit_mask = EX_DMA_SYSTEM_WRITE_ECC;
		break;
	case FMAN_EX_DMA_FM_WRITE_ECC:
		bit_mask = EX_DMA_FM_WRITE_ECC;
		break;
	case FMAN_EX_FPM_STALL_ON_TASKS:
		bit_mask = EX_FPM_STALL_ON_TASKS;
		break;
	case FMAN_EX_FPM_SINGLE_ECC:
		bit_mask = EX_FPM_SINGLE_ECC;
		break;
	case FMAN_EX_FPM_DOUBLE_ECC:
		bit_mask = EX_FPM_DOUBLE_ECC;
		break;
	case FMAN_EX_QMI_SINGLE_ECC:
		bit_mask = EX_QMI_SINGLE_ECC;
		break;
	case FMAN_EX_QMI_DOUBLE_ECC:
		bit_mask = EX_QMI_DOUBLE_ECC;
		break;
	case FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID:
		bit_mask = EX_QMI_DEQ_FROM_UNKNOWN_PORTID;
		break;
	case FMAN_EX_BMI_LIST_RAM_ECC:
		bit_mask = EX_BMI_LIST_RAM_ECC;
		break;
	case FMAN_EX_BMI_STORAGE_PROFILE_ECC:
		bit_mask = EX_BMI_STORAGE_PROFILE_ECC;
		break;
	case FMAN_EX_BMI_STATISTICS_RAM_ECC:
		bit_mask = EX_BMI_STATISTICS_RAM_ECC;
		break;
	case FMAN_EX_BMI_DISPATCH_RAM_ECC:
		bit_mask = EX_BMI_DISPATCH_RAM_ECC;
		break;
	case FMAN_EX_MURAM_ECC:
		bit_mask = EX_MURAM_ECC;
		break;
	default:
		bit_mask = 0;
		break;
	}

	return bit_mask;
}

static int get_module_event(enum fman_event_modules module, u8 mod_id,
			    enum fman_intr_type intr_type)
{
	int event;

	switch (module) {
	case FMAN_MOD_MAC:
			event = (intr_type == FMAN_INTR_TYPE_ERR) ?
			(FMAN_EV_ERR_MAC0 + mod_id) :
			(FMAN_EV_MAC0 + mod_id);
		break;
	case FMAN_MOD_FMAN_CTRL:
		if (intr_type == FMAN_INTR_TYPE_ERR)
			event = FMAN_EV_CNT;
		else
			event = (FMAN_EV_FMAN_CTRL_0 + mod_id);
		break;
	case FMAN_MOD_DUMMY_LAST:
		event = FMAN_EV_CNT;
		break;
	default:
		event = FMAN_EV_CNT;
		break;
	}

	return event;
}

static int set_size_of_fifo(struct fman *fman, u8 port_id, u32 *size_of_fifo,
			    u32 *extra_size_of_fifo)
{
	struct fman_bmi_regs __iomem *bmi_rg = fman->bmi_regs;
	u32 fifo = *size_of_fifo;
	u32 extra_fifo = *extra_size_of_fifo;
	u32 tmp;

	/* if this is the first time a port requires extra_fifo_pool_size,
	 * the total extra_fifo_pool_size must be initialized to 1 buffer per
	 * port
	 */
	if (extra_fifo && !fman->state->extra_fifo_pool_size)
		fman->state->extra_fifo_pool_size =
			fman->state->num_of_rx_ports * FMAN_BMI_FIFO_UNITS;

	fman->state->extra_fifo_pool_size =
		max(fman->state->extra_fifo_pool_size, extra_fifo);

	/* check that there are enough uncommitted fifo size */
	if ((fman->state->accumulated_fifo_size + fifo) >
	    (fman->state->total_fifo_size -
	    fman->state->extra_fifo_pool_size)) {
		pr_err("Requested fifo size and extra size exceed total FIFO size.\n");
		return -EAGAIN;
	}

	/* Read, modify and write to HW */
	tmp = (u32)((fifo / FMAN_BMI_FIFO_UNITS - 1) |
		    ((extra_fifo / FMAN_BMI_FIFO_UNITS) <<
		    BMI_EXTRA_FIFO_SIZE_SHIFT));
	iowrite32be(tmp, &bmi_rg->fmbm_pfs[port_id - 1]);

	/* update accumulated */
	fman->state->accumulated_fifo_size += fifo;

	return 0;
}

static int set_num_of_tasks(struct fman *fman, u8 port_id, u8 *num_of_tasks,
			    u8 *num_of_extra_tasks)
{
	struct fman_bmi_regs __iomem *bmi_rg = fman->bmi_regs;
	u8 tasks = *num_of_tasks;
	u8 extra_tasks = *num_of_extra_tasks;
	u32 tmp;

	if (extra_tasks)
		fman->state->extra_tasks_pool_size =
		(u8)max(fman->state->extra_tasks_pool_size, extra_tasks);

	/* check that there are enough uncommitted tasks */
	if ((fman->state->accumulated_num_of_tasks + tasks) >
	    (fman->state->total_num_of_tasks -
	     fman->state->extra_tasks_pool_size)) {
		pr_err("Requested num_of_tasks and extra tasks pool for fm%d exceed total num_of_tasks.\n",
		       fman->state->fm_id);
		return -EAGAIN;
	}
	/* update accumulated */
	fman->state->accumulated_num_of_tasks += tasks;

	/* Write to HW */
	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]) &
	    ~(BMI_NUM_OF_TASKS_MASK | BMI_NUM_OF_EXTRA_TASKS_MASK);
	tmp |= ((u32)((tasks - 1) << BMI_NUM_OF_TASKS_SHIFT) |
		(u32)(extra_tasks << BMI_EXTRA_NUM_OF_TASKS_SHIFT));
	iowrite32be(tmp, &bmi_rg->fmbm_pp[port_id - 1]);

	return 0;
}

static int set_num_of_open_dmas(struct fman *fman, u8 port_id,
				u8 *num_of_open_dmas,
				u8 *num_of_extra_open_dmas)
{
	struct fman_bmi_regs __iomem *bmi_rg = fman->bmi_regs;
	u8 open_dmas = *num_of_open_dmas;
	u8 extra_open_dmas = *num_of_extra_open_dmas;
	u8 total_num_dmas = 0, current_val = 0, current_extra_val = 0;
	u32 tmp;

	if (!open_dmas) {
		/* Configuration according to values in the HW.
		 * read the current number of open Dma's
		 */
		tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]);
		current_extra_val = (u8)((tmp & BMI_NUM_OF_EXTRA_DMAS_MASK) >>
					 BMI_EXTRA_NUM_OF_DMAS_SHIFT);

		tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]);
		current_val = (u8)(((tmp & BMI_NUM_OF_DMAS_MASK) >>
				   BMI_NUM_OF_DMAS_SHIFT) + 1);

		/* This is the first configuration and user did not
		 * specify value (!open_dmas), reset values will be used
		 * and we just save these values for resource management
		 */
		fman->state->extra_open_dmas_pool_size =
			(u8)max(fman->state->extra_open_dmas_pool_size,
				current_extra_val);
		fman->state->accumulated_num_of_open_dmas += current_val;
		*num_of_open_dmas = current_val;
		*num_of_extra_open_dmas = current_extra_val;
		return 0;
	}

	if (extra_open_dmas > current_extra_val)
		fman->state->extra_open_dmas_pool_size =
		    (u8)max(fman->state->extra_open_dmas_pool_size,
			    extra_open_dmas);

	if ((fman->state->rev_info.major < 6) &&
	    (fman->state->accumulated_num_of_open_dmas - current_val +
	     open_dmas > fman->state->max_num_of_open_dmas)) {
		pr_err("Requested num_of_open_dmas for fm%d exceeds total num_of_open_dmas.\n",
		       fman->state->fm_id);
		return -EAGAIN;
	} else if ((fman->state->rev_info.major >= 6) &&
		   !((fman->state->rev_info.major == 6) &&
		   (fman->state->rev_info.minor == 0)) &&
		   (fman->state->accumulated_num_of_open_dmas -
		   current_val + open_dmas >
		   fman->state->dma_thresh_max_commq + 1)) {
		pr_err("Requested num_of_open_dmas for fm%d exceeds DMA Command queue (%d)\n",
		       fman->state->fm_id,
		       fman->state->dma_thresh_max_commq + 1);
		return -EAGAIN;
	}

	WARN_ON(fman->state->accumulated_num_of_open_dmas < current_val);
	/* update acummulated */
	fman->state->accumulated_num_of_open_dmas -= current_val;
	fman->state->accumulated_num_of_open_dmas += open_dmas;

	if (fman->state->rev_info.major < 6)
		total_num_dmas =
		    (u8)(fman->state->accumulated_num_of_open_dmas +
		    fman->state->extra_open_dmas_pool_size);

	/* calculate reg */
	tmp = ioread32be(&bmi_rg->fmbm_pp[port_id - 1]) &
	    ~(BMI_NUM_OF_DMAS_MASK | BMI_NUM_OF_EXTRA_DMAS_MASK);
	tmp |= (u32)(((open_dmas - 1) << BMI_NUM_OF_DMAS_SHIFT) |
			   (extra_open_dmas << BMI_EXTRA_NUM_OF_DMAS_SHIFT));
	iowrite32be(tmp, &bmi_rg->fmbm_pp[port_id - 1]);

	/* update total num of DMA's with committed number of open DMAS,
	 * and max uncommitted pool.
	 */
	if (total_num_dmas) {
		tmp = ioread32be(&bmi_rg->fmbm_cfg2) & ~BMI_CFG2_DMAS_MASK;
		tmp |= (u32)(total_num_dmas - 1) << BMI_CFG2_DMAS_SHIFT;
		iowrite32be(tmp, &bmi_rg->fmbm_cfg2);
	}

	return 0;
}

static int fman_config(struct fman *fman)
{
	void __iomem *base_addr;
	int err;

	base_addr = fman->dts_params.base_addr;

	fman->state = kzalloc(sizeof(*fman->state), GFP_KERNEL);
	if (!fman->state)
		goto err_fm_state;

	/* Allocate the FM driver's parameters structure */
	fman->cfg = kzalloc(sizeof(*fman->cfg), GFP_KERNEL);
	if (!fman->cfg)
		goto err_fm_drv;

	/* Initialize MURAM block */
	fman->muram = fman_muram_init(fman->dts_params.muram_phy_base_addr,
				      fman->dts_params.muram_size);
	if (!fman->muram)
		goto err_fm_soc_specific;

	/* Initialize FM parameters which will be kept by the driver */
	fman->state->fm_id = fman->dts_params.id;
	fman->state->fm_clk_freq = fman->dts_params.clk_freq;
	fman->state->qman_channel_base = fman->dts_params.qman_channel_base;
	fman->state->num_of_qman_channels =
		fman->dts_params.num_of_qman_channels;
#ifndef __rtems__
	fman->state->res = fman->dts_params.res;
#endif /* __rtems__ */
	fman->exception_cb = fman_exceptions;
	fman->bus_error_cb = fman_bus_error;
	fman->fpm_regs =
		(struct fman_fpm_regs __iomem *)(base_addr + FPM_OFFSET);
	fman->bmi_regs =
		(struct fman_bmi_regs __iomem *)(base_addr + BMI_OFFSET);
	fman->qmi_regs =
		(struct fman_qmi_regs __iomem *)(base_addr + QMI_OFFSET);
	fman->dma_regs =
		(struct fman_dma_regs __iomem *)(base_addr + DMA_OFFSET);
	fman->base_addr = base_addr;

	spin_lock_init(&fman->spinlock);
	fman_defconfig(fman->cfg);

	fman->cfg->qmi_deq_option_support = true;

	fman->state->extra_fifo_pool_size = 0;
	fman->state->exceptions = DFLT_EXCEPTIONS;
	fman->reset_on_init = true;

	/* Read FMan revision for future use*/
	fman_get_revision(fman, &fman->state->rev_info);

	err = fill_soc_specific_params(fman->state);
	if (err)
		goto err_fm_soc_specific;

	/* FM_AID_MODE_NO_TNUM_SW005 Errata workaround */
	if (fman->state->rev_info.major >= 6)
		fman->cfg->dma_aid_mode = FMAN_DMA_AID_OUT_PORT_ID;

	fman->cfg->qmi_def_tnums_thresh = fman->state->qmi_def_tnums_thresh;

	fman->state->total_num_of_tasks =
	(u8)DFLT_TOTAL_NUM_OF_TASKS(fman->state->rev_info.major,
				    fman->state->rev_info.minor,
				    fman->state->bmi_max_num_of_tasks);

	if (fman->state->rev_info.major < 6) {
		fman->cfg->dma_comm_qtsh_clr_emer =
		(u8)DFLT_DMA_COMM_Q_LOW(fman->state->rev_info.major,
					fman->state->dma_thresh_max_commq);

		fman->cfg->dma_comm_qtsh_asrt_emer =
		(u8)DFLT_DMA_COMM_Q_HIGH(fman->state->rev_info.major,
					 fman->state->dma_thresh_max_commq);

		fman->cfg->dma_cam_num_of_entries =
		DFLT_DMA_CAM_NUM_OF_ENTRIES(fman->state->rev_info.major);

		fman->cfg->dma_read_buf_tsh_clr_emer =
		DFLT_DMA_READ_INT_BUF_LOW(fman->state->dma_thresh_max_buf);

		fman->cfg->dma_read_buf_tsh_asrt_emer =
		DFLT_DMA_READ_INT_BUF_HIGH(fman->state->dma_thresh_max_buf);

		fman->cfg->dma_write_buf_tsh_clr_emer =
		DFLT_DMA_WRITE_INT_BUF_LOW(fman->state->dma_thresh_max_buf);

		fman->cfg->dma_write_buf_tsh_asrt_emer =
		DFLT_DMA_WRITE_INT_BUF_HIGH(fman->state->dma_thresh_max_buf);

		fman->cfg->dma_axi_dbg_num_of_beats =
		DFLT_AXI_DBG_NUM_OF_BEATS;
	}

	return 0;

err_fm_soc_specific:
	kfree(fman->cfg);
err_fm_drv:
	kfree(fman->state);
err_fm_state:
	kfree(fman);
	return -EINVAL;
}

static int fman_init(struct fman *fman)
{
	struct fman_cfg *cfg = NULL;
	struct fman_rg fman_rg;
	int err = 0, i;

	if (is_init_done(fman->cfg))
		return -EINVAL;

	fman_rg.bmi_rg = fman->bmi_regs;
	fman_rg.qmi_rg = fman->qmi_regs;
	fman_rg.fpm_rg = fman->fpm_regs;
	fman_rg.dma_rg = fman->dma_regs;

	fman->state->count1_micro_bit = FM_TIMESTAMP_1_USEC_BIT;

	cfg = fman->cfg;

	/* clear revision-dependent non existing exception */
	if (fman->state->rev_info.major < 6)
		fman->state->exceptions &= ~FMAN_EX_BMI_DISPATCH_RAM_ECC;

	if (fman->state->rev_info.major >= 6)
		fman->state->exceptions &= ~FMAN_EX_QMI_SINGLE_ECC;

	/* clear CPG */
	memset_io((void __iomem *)(fman->base_addr + CGP_OFFSET), 0,
		  fman->state->fm_port_num_of_cg);

	/* Save LIODN info before FMan reset
	 * Skipping non-existent port 0 (i = 1)
	 */
	for (i = 1; i < FMAN_LIODN_TBL; i++) {
		u32 liodn_base;

		fman->liodn_offset[i] =
			ioread32be(&fman_rg.bmi_rg->fmbm_spliodn[i - 1]);
		liodn_base = ioread32be(&fman_rg.dma_rg->fmdmplr[i / 2]);
		if (i % 2) {
			/* FMDM_PLR LSB holds LIODN base for odd ports */
			liodn_base &= DMA_LIODN_BASE_MASK;
		} else {
			/* FMDM_PLR MSB holds LIODN base for even ports */
			liodn_base >>= DMA_LIODN_SHIFT;
			liodn_base &= DMA_LIODN_BASE_MASK;
		}
		fman->liodn_base[i] = liodn_base;
	}

	/* Reset the FM if required. */
	if (fman->reset_on_init) {
		if (fman->state->rev_info.major >= 6) {
			/* Errata A007273 */
			pr_debug("FManV3 reset is not supported!\n");
		} else {
			out_be32(&fman->fpm_regs->fm_rstc, FPM_RSTC_FM_RESET);
			/* Memory barrier */
			mb();
			usleep_range(100, 300);
		}

		if (!!(ioread32be(&fman_rg.qmi_rg->fmqm_gs) &
		    QMI_GS_HALT_NOT_BUSY)) {
			resume(fman->fpm_regs);
			usleep_range(100, 300);
		}
	}

	if (clear_iram(fman) != 0)
		return -EINVAL;

	cfg->exceptions = fman->state->exceptions;

	/* Init DMA Registers */

	err = dma_init(fman);
	if (err != 0) {
		free_init_resources(fman);
		return err;
	}

	/* Init FPM Registers */
	fpm_init(fman->fpm_regs, fman->cfg);

	/* define common resources */
	/* allocate MURAM for FIFO according to total size */
	fman->fifo_offset = fman_muram_alloc(fman->muram,
					     fman->state->total_fifo_size);
	if (IS_ERR_VALUE(fman->cam_offset)) {
		free_init_resources(fman);
		pr_err("MURAM alloc for BMI FIFO failed\n");
		return -ENOMEM;
	}

	cfg->fifo_base_addr = fman->fifo_offset;
	cfg->total_fifo_size = fman->state->total_fifo_size;
	cfg->total_num_of_tasks = fman->state->total_num_of_tasks;
	cfg->clk_freq = fman->state->fm_clk_freq;

	/* Init BMI Registers */
	bmi_init(fman->bmi_regs, fman->cfg);

	/* Init QMI Registers */
	qmi_init(fman->qmi_regs, fman->cfg);

	err = enable(&fman_rg, cfg);
	if (err != 0)
		return err;

	enable_time_stamp(fman);

	kfree(fman->cfg);
	fman->cfg = NULL;

	return 0;
}

static int fman_set_exception(struct fman *fman,
			      enum fman_exceptions exception, bool enable)
{
	u32 bit_mask = 0;
	struct fman_rg fman_rg;

	if (!is_init_done(fman->cfg))
		return -EINVAL;

	fman_rg.bmi_rg = fman->bmi_regs;
	fman_rg.qmi_rg = fman->qmi_regs;
	fman_rg.fpm_rg = fman->fpm_regs;
	fman_rg.dma_rg = fman->dma_regs;

	bit_mask = get_exception_flag(exception);
	if (bit_mask) {
		if (enable)
			fman->state->exceptions |= bit_mask;
		else
			fman->state->exceptions &= ~bit_mask;
	} else {
		pr_err("Undefined exception\n");
		return -EINVAL;
	}

	return set_exception(&fman_rg, exception, enable);
}

void fman_register_intr(struct fman *fman, enum fman_event_modules module,
			u8 mod_id, enum fman_intr_type intr_type,
			void (*isr_cb)(void *src_arg), void *src_arg)
{
	int event = 0;

	event = get_module_event(module, mod_id, intr_type);
	WARN_ON(!(event < FMAN_EV_CNT));

	/* register in local FM structure */
	fman->intr_mng[event].isr_cb = isr_cb;
	fman->intr_mng[event].src_handle = src_arg;
}

void fman_unregister_intr(struct fman *fman, enum fman_event_modules module,
			  u8 mod_id, enum fman_intr_type intr_type)
{
	int event = 0;

	event = get_module_event(module, mod_id, intr_type);
	WARN_ON(!(event < FMAN_EV_CNT));

	fman->intr_mng[event].isr_cb = NULL;
	fman->intr_mng[event].src_handle = NULL;
}

int fman_set_port_params(struct fman *fman,
			 struct fman_port_init_params *port_params)
{
	int err;
	unsigned long int_flags;
	u8 port_id = port_params->port_id, mac_id;
	struct fman_rg fman_rg;

	fman_rg.bmi_rg = fman->bmi_regs;
	fman_rg.qmi_rg = fman->qmi_regs;
	fman_rg.fpm_rg = fman->fpm_regs;
	fman_rg.dma_rg = fman->dma_regs;

	spin_lock_irqsave(&fman->spinlock, int_flags);

	err = set_num_of_tasks(fman, port_params->port_id,
			       &port_params->num_of_tasks,
			       &port_params->num_of_extra_tasks);
	if (err) {
		spin_unlock_irqrestore(&fman->spinlock, int_flags);
		return err;
	}

	/* TX Ports */
	if (port_params->port_type != FMAN_PORT_TYPE_RX) {
		u32 enq_th, deq_th, reg;

		/* update qmi ENQ/DEQ threshold */
		fman->state->accumulated_num_of_deq_tnums +=
			port_params->deq_pipeline_depth;
		enq_th = (ioread32be(&fman_rg.qmi_rg->fmqm_gc) &
			  QMI_CFG_ENQ_MASK) >> QMI_CFG_ENQ_SHIFT;
		/* if enq_th is too big, we reduce it to the max value
		 * that is still 0
		 */
		if (enq_th >= (fman->state->qmi_max_num_of_tnums -
		    fman->state->accumulated_num_of_deq_tnums)) {
			enq_th =
			fman->state->qmi_max_num_of_tnums -
			fman->state->accumulated_num_of_deq_tnums - 1;

			reg = ioread32be(&fman_rg.qmi_rg->fmqm_gc);
			reg &= ~QMI_CFG_ENQ_MASK;
			reg |= (enq_th << QMI_CFG_ENQ_SHIFT);
			iowrite32be(reg, &fman_rg.qmi_rg->fmqm_gc);
		}

		deq_th = ioread32be(&fman_rg.qmi_rg->fmqm_gc) &
				    QMI_CFG_DEQ_MASK;
		/* if deq_th is too small, we enlarge it to the min
		 * value that is still 0.
		 * depTh may not be larger than 63
		 * (fman->state->qmi_max_num_of_tnums-1).
		 */
		if ((deq_th <= fman->state->accumulated_num_of_deq_tnums) &&
		    (deq_th < fman->state->qmi_max_num_of_tnums - 1)) {
				deq_th =
				fman->state->accumulated_num_of_deq_tnums + 1;
			reg = ioread32be(&fman_rg.qmi_rg->fmqm_gc);
			reg &= ~QMI_CFG_DEQ_MASK;
			reg |= deq_th;
			iowrite32be(reg, &fman_rg.qmi_rg->fmqm_gc);
		}
	}

	err = set_size_of_fifo(fman, port_params->port_id,
			       &port_params->size_of_fifo,
			       &port_params->extra_size_of_fifo);
	if (err) {
		spin_unlock_irqrestore(&fman->spinlock, int_flags);
		return err;
	}

	err = set_num_of_open_dmas(fman, port_params->port_id,
				   &port_params->num_of_open_dmas,
				   &port_params->num_of_extra_open_dmas);
	if (err) {
		spin_unlock_irqrestore(&fman->spinlock, int_flags);
		return err;
	}

	set_port_liodn(&fman_rg, port_id, fman->liodn_base[port_id],
		       fman->liodn_offset[port_id]);

	if (fman->state->rev_info.major < 6)
		set_port_order_restoration(fman_rg.fpm_rg, port_id);

	mac_id = hw_port_id_to_sw_port_id(fman->state->rev_info.major, port_id);

	if (port_params->max_frame_length >= fman->state->mac_mfl[mac_id]) {
		fman->state->port_mfl[mac_id] = port_params->max_frame_length;
	} else {
		pr_warn("Port max_frame_length is smaller than MAC current MTU\n");
		spin_unlock_irqrestore(&fman->spinlock, int_flags);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&fman->spinlock, int_flags);

	return 0;
}

int fman_reset_mac(struct fman *fman, u8 mac_id)
{
	struct fman_fpm_regs __iomem *fpm_rg = fman->fpm_regs;
	u32 msk, timeout = 100;

	if (fman->state->rev_info.major >= 6) {
		pr_warn("FMan MAC reset no available for FMan V3!\n");
		return -EINVAL;
	}

	/* Get the relevant bit mask */
	switch (mac_id) {
	case 0:
		msk = FPM_RSTC_MAC0_RESET;
		break;
	case 1:
		msk = FPM_RSTC_MAC1_RESET;
		break;
	case 2:
		msk = FPM_RSTC_MAC2_RESET;
		break;
	case 3:
		msk = FPM_RSTC_MAC3_RESET;
		break;
	case 4:
		msk = FPM_RSTC_MAC4_RESET;
		break;
	case 5:
		msk = FPM_RSTC_MAC5_RESET;
		break;
	case 6:
		msk = FPM_RSTC_MAC6_RESET;
		break;
	case 7:
		msk = FPM_RSTC_MAC7_RESET;
		break;
	case 8:
		msk = FPM_RSTC_MAC8_RESET;
		break;
	case 9:
		msk = FPM_RSTC_MAC9_RESET;
		break;
	default:
		pr_warn("Illegal MAC Id\n");
		return -EINVAL;
	}

	/* reset */
	iowrite32be(msk, &fpm_rg->fm_rstc);
	while ((ioread32be(&fpm_rg->fm_rstc) & msk) && --timeout)
		udelay(10);

	if (!timeout)
		return -EIO;

	return 0;
}

int fman_set_mac_max_frame(struct fman *fman, u8 mac_id, u16 mfl)
{
	/* if port is already initialized, check that MaxFrameLength is smaller
	 * or equal to the port's max
	 */
	if ((!fman->state->port_mfl[mac_id]) ||
	    (fman->state->port_mfl[mac_id] &&
	    (mfl <= fman->state->port_mfl[mac_id]))) {
		fman->state->mac_mfl[mac_id] = mfl;
	} else {
		pr_warn("MAC max_frame_length is larger than Port max_frame_length\n");
		return -EINVAL;
	}
	return 0;
}

u16 fman_get_clock_freq(struct fman *fman)
{
	return fman->state->fm_clk_freq;
}

u32 fman_get_bmi_max_fifo_size(struct fman *fman)
{
	return fman->state->bmi_max_fifo_size;
}

static void fman_event_isr(struct fman *fman)
{
	u32 pending;
	struct fman_fpm_regs __iomem *fpm_rg;

	if (!is_init_done(fman->cfg))
		return;

	fpm_rg = fman->fpm_regs;

	/* normal interrupts */
	pending = ioread32be(&fpm_rg->fm_npi);
	if (!pending)
		return;

	if (pending & INTR_EN_QMI)
		qmi_event(fman);

	/* MAC interrupts */
	if (pending & INTR_EN_MAC0)
		call_mac_isr(fman, FMAN_EV_MAC0 + 0);
	if (pending & INTR_EN_MAC1)
		call_mac_isr(fman, FMAN_EV_MAC0 + 1);
	if (pending & INTR_EN_MAC2)
		call_mac_isr(fman, FMAN_EV_MAC0 + 2);
	if (pending & INTR_EN_MAC3)
		call_mac_isr(fman, FMAN_EV_MAC0 + 3);
	if (pending & INTR_EN_MAC4)
		call_mac_isr(fman, FMAN_EV_MAC0 + 4);
	if (pending & INTR_EN_MAC5)
		call_mac_isr(fman, FMAN_EV_MAC0 + 5);
	if (pending & INTR_EN_MAC6)
		call_mac_isr(fman, FMAN_EV_MAC0 + 6);
	if (pending & INTR_EN_MAC7)
		call_mac_isr(fman, FMAN_EV_MAC0 + 7);
	if (pending & INTR_EN_MAC8)
		call_mac_isr(fman, FMAN_EV_MAC0 + 8);
	if (pending & INTR_EN_MAC9)
		call_mac_isr(fman, FMAN_EV_MAC0 + 9);
}

static int fman_error_isr(struct fman *fman)
{
	u32 pending;
	struct fman_fpm_regs __iomem *fpm_rg;

	if (!is_init_done(fman->cfg))
		return -EINVAL;

	fpm_rg = fman->fpm_regs;

	/* error interrupts */
	pending = ioread32be(&fpm_rg->fm_epi);
	if (!pending)
		return -EINVAL;

	if (pending & ERR_INTR_EN_BMI)
		bmi_err_event(fman);
	if (pending & ERR_INTR_EN_QMI)
		qmi_err_event(fman);
	if (pending & ERR_INTR_EN_FPM)
		fpm_err_event(fman);
	if (pending & ERR_INTR_EN_DMA)
		dma_err_event(fman);
	if (pending & ERR_INTR_EN_MURAM)
		muram_err_intr(fman);

	/* MAC error interrupts */
	if (pending & ERR_INTR_EN_MAC0)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 0);
	if (pending & ERR_INTR_EN_MAC1)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 1);
	if (pending & ERR_INTR_EN_MAC2)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 2);
	if (pending & ERR_INTR_EN_MAC3)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 3);
	if (pending & ERR_INTR_EN_MAC4)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 4);
	if (pending & ERR_INTR_EN_MAC5)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 5);
	if (pending & ERR_INTR_EN_MAC6)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 6);
	if (pending & ERR_INTR_EN_MAC7)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 7);
	if (pending & ERR_INTR_EN_MAC8)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 8);
	if (pending & ERR_INTR_EN_MAC9)
		call_mac_isr(fman, FMAN_EV_ERR_MAC0 + 9);

	return 0;
}

void fman_get_revision(struct fman *fman, struct fman_rev_info *rev_info)
{
	u32 tmp;

	tmp = ioread32be(&fman->fpm_regs->fm_ip_rev_1);
	rev_info->major = (u8)((tmp & FPM_REV1_MAJOR_MASK) >>
				FPM_REV1_MAJOR_SHIFT);
	rev_info->minor = tmp & FPM_REV1_MINOR_MASK;
}

u32 fman_get_qman_channel_id(struct fman *fman, u32 port_id)
{
	int i;

	if (fman->state->rev_info.major >= 6) {
		u32 port_ids[] = {0x30, 0x31, 0x28, 0x29, 0x2a, 0x2b,
				  0x2c, 0x2d, 0x2, 0x3, 0x4, 0x5, 0x7, 0x7};
		for (i = 0; i < fman->state->num_of_qman_channels; i++) {
			if (port_ids[i] == port_id)
				break;
		}
	} else {
		u32 port_ids[] = {0x30, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x1,
				  0x2, 0x3, 0x4, 0x5, 0x7, 0x7};
		for (i = 0; i < fman->state->num_of_qman_channels; i++) {
			if (port_ids[i] == port_id)
				break;
		}
	}

	if (i == fman->state->num_of_qman_channels)
		return 0;

	return fman->state->qman_channel_base + i;
}

struct resource *fman_get_mem_region(struct fman *fman)
{
	return fman->state->res;
}

/* Bootargs defines */
/* Extra headroom for RX buffers - Default, min and max */
#define FSL_FM_RX_EXTRA_HEADROOM	64
#define FSL_FM_RX_EXTRA_HEADROOM_MIN	16
#define FSL_FM_RX_EXTRA_HEADROOM_MAX	384

/* Maximum frame length */
#define FSL_FM_MAX_FRAME_SIZE			1522
#define FSL_FM_MAX_POSSIBLE_FRAME_SIZE		9600
#define FSL_FM_MIN_POSSIBLE_FRAME_SIZE		64

/* Extra headroom for Rx buffers.
 * FMan is instructed to allocate, on the Rx path, this amount of
 * space at the beginning of a data buffer, beside the DPA private
 * data area and the IC fields.
 * Does not impact Tx buffer layout.
 * Configurable from bootargs. 64 by default, it's needed on
 * particular forwarding scenarios that add extra headers to the
 * forwarded frame.
 */
int fsl_fm_rx_extra_headroom = FSL_FM_RX_EXTRA_HEADROOM;
module_param(fsl_fm_rx_extra_headroom, int, 0);
MODULE_PARM_DESC(fsl_fm_rx_extra_headroom, "Extra headroom for Rx buffers");

/* Max frame size, across all interfaces.
 * Configurable from bootargs, to avoid allocating oversized (socket)
 * buffers when not using jumbo frames.
 * Must be large enough to accommodate the network MTU, but small enough
 * to avoid wasting skb memory.
 *
 * Could be overridden once, at boot-time, via the
 * fm_set_max_frm() callback.
 */
#ifndef __rtems__
int fsl_fm_max_frm = FSL_FM_MAX_FRAME_SIZE;
#else /* __rtems__ */
int fsl_fm_max_frm = FSL_FM_MAX_POSSIBLE_FRAME_SIZE;
#endif /* __rtems__ */
module_param(fsl_fm_max_frm, int, 0);
MODULE_PARM_DESC(fsl_fm_max_frm, "Maximum frame size, across all interfaces");

u16 fman_get_max_frm(void)
{
	static bool fm_check_mfl;

	if (!fm_check_mfl) {
		if (fsl_fm_max_frm > FSL_FM_MAX_POSSIBLE_FRAME_SIZE ||
		    fsl_fm_max_frm < FSL_FM_MIN_POSSIBLE_FRAME_SIZE) {
			pr_warn("Invalid fsl_fm_max_frm value (%d) in bootargs, valid range is %d-%d. Falling back to the default (%d)\n",
				fsl_fm_max_frm,
				FSL_FM_MIN_POSSIBLE_FRAME_SIZE,
				FSL_FM_MAX_POSSIBLE_FRAME_SIZE,
				FSL_FM_MAX_FRAME_SIZE);
			fsl_fm_max_frm = FSL_FM_MAX_FRAME_SIZE;
		}
		fm_check_mfl = true;
	}

	return fsl_fm_max_frm;
}
EXPORT_SYMBOL(fman_get_max_frm);

int fman_get_rx_extra_headroom(void)
{
	static bool fm_check_rx_extra_headroom;

	if (!fm_check_rx_extra_headroom) {
		if (fsl_fm_rx_extra_headroom > FSL_FM_RX_EXTRA_HEADROOM_MAX ||
		    fsl_fm_rx_extra_headroom < FSL_FM_RX_EXTRA_HEADROOM_MIN) {
			pr_warn("Invalid fsl_fm_rx_extra_headroom value (%d) in bootargs, valid range is %d-%d. Falling back to the default (%d)\n",
				fsl_fm_rx_extra_headroom,
				FSL_FM_RX_EXTRA_HEADROOM_MIN,
				FSL_FM_RX_EXTRA_HEADROOM_MAX,
				FSL_FM_RX_EXTRA_HEADROOM);
			fsl_fm_rx_extra_headroom = FSL_FM_RX_EXTRA_HEADROOM;
		}

		fsl_fm_rx_extra_headroom = true;
		fsl_fm_rx_extra_headroom = ALIGN(fsl_fm_rx_extra_headroom, 16);
	}

	return fsl_fm_rx_extra_headroom;
}
EXPORT_SYMBOL(fman_get_rx_extra_headroom);

struct fman *fman_bind(struct device *fm_dev)
{
	return (struct fman *)(dev_get_drvdata(get_device(fm_dev)));
}

void fman_unbind(struct fman *fman)
{
	put_device(fman->dev);
}

struct device *fman_get_device(struct fman *fman)
{
	return fman->dev;
}

static irqreturn_t fman_irq(int irq, void *fman)
{
	fman_event_isr(fman);

	return IRQ_HANDLED;
}

#ifndef __rtems__
static const struct of_device_id fman_muram_match[] = {
	{
	 .compatible = "fsl,fman-muram"},
	{}
};
MODULE_DEVICE_TABLE(of, fman_muram_match);
#endif /* __rtems__ */

static struct fman *read_dts_node(struct platform_device *of_dev)
{
	struct fman *fman;
#ifndef __rtems__
	struct device_node *fm_node, *muram_node;
	struct resource *res;
#else /* __rtems__ */
	const char *fdt = bsp_fdt_get();
	struct device_node *fm_node;
#endif /* __rtems__ */
	const u32 *u32_prop;
	int lenp, err, irq;
#ifndef __rtems__
	struct clk *clk;
	u32 clk_rate;
#endif /* __rtems__ */
	phys_addr_t phys_base_addr;
#ifndef __rtems__
	resource_size_t mem_size;
#endif /* __rtems__ */

	fman = kzalloc(sizeof(*fman), GFP_KERNEL);
	if (!fman)
		return NULL;

	fm_node = of_node_get(of_dev->dev.of_node);

	u32_prop = (const u32 *)of_get_property(fm_node, "cell-index", &lenp);
	if (!u32_prop) {
		pr_err("of_get_property(%s, cell-index) failed\n",
		       fm_node->full_name);
		goto fman_node_put;
	}
	if (WARN_ON(lenp != sizeof(u32)))
		goto fman_node_put;

	fman->dts_params.id = (u8)*u32_prop;

#ifndef __rtems__
	/* Get the FM interrupt */
	res = platform_get_resource(of_dev, IORESOURCE_IRQ, 0);
	if (!res) {
		pr_err("Can't get FMan IRQ resource\n");
		goto fman_node_put;
	}
	irq = res->start;

	/* Get the FM error interrupt */
	res = platform_get_resource(of_dev, IORESOURCE_IRQ, 1);
	if (!res) {
		pr_err("Can't get FMan Error IRQ resource\n");
		goto fman_node_put;
	}
	fman->dts_params.err_irq = res->start;

	/* Get the FM address */
	res = platform_get_resource(of_dev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("Can't get FMan memory resouce\n");
		goto fman_node_put;
	}

	phys_base_addr = res->start;
	mem_size = res->end + 1 - res->start;
#else /* __rtems__ */
	irq = of_irq_to_resource(fm_node, 0, NULL);
	fman->dts_params.err_irq = of_irq_to_resource(fm_node, 1, NULL);
	phys_base_addr = of_dev->dev.base;
	fman->dts_params.base_addr = (void *)(uintptr_t)phys_base_addr;
#endif /* __rtems__ */

#ifndef __rtems__
	clk = of_clk_get_by_name(fm_node, NULL);
	if (IS_ERR(clk)) {
		pr_err("Failed to get FM%d clock structure\n",
		       fman->dts_params.id);
		goto fman_node_put;
	}

	clk_rate = clk_get_rate(clk);
	if (!clk_rate) {
		pr_err("Failed to determine FM%d clock rate\n",
		       fman->dts_params.id);
		goto fman_node_put;
	}
	/* Rounding to MHz */
	fman->dts_params.clk_freq = (u16)((clk_rate + 500000) / 1000000);
#else /* __rtems__ */
	/* FIXME */
	fman->dts_params.clk_freq = 733;
#endif /* __rtems__ */

	u32_prop = (const u32 *)of_get_property(fm_node,
						"fsl,qman-channel-range",
						&lenp);
	if (!u32_prop) {
		pr_err("of_get_property(%s, fsl,qman-channel-range) failed\n",
		       fm_node->full_name);
		goto fman_node_put;
	}
	if (WARN_ON(lenp != sizeof(u32) * 2))
		goto fman_node_put;
	fman->dts_params.qman_channel_base = u32_prop[0];
	fman->dts_params.num_of_qman_channels = u32_prop[1];

	/* Get the MURAM base address and size */
#ifndef __rtems__
	/* FIXME */
	muram_node = of_find_matching_node(fm_node, fman_muram_match);
	if (!muram_node) {
		pr_err("could not find MURAM node\n");
		goto fman_node_put;
	}

	err = of_address_to_resource(muram_node, 0, res);
	if (err) {
		of_node_put(muram_node);
		pr_err("of_address_to_resource() = %d\n", err);
		goto fman_node_put;
	}

	fman->dts_params.muram_phy_base_addr = res->start;
	fman->dts_params.muram_size = res->end + 1 - res->start;
#else /* __rtems__ */
	{
		int node = fdt_node_offset_by_compatible(fdt,
		    fm_node->offset, "fsl,fman-muram");
		struct device_node muram_node = {
			.offset = node
		};
		struct resource res;

		err = of_address_to_resource(&muram_node, 0, &res);
		if (err != 0) {
			pr_err("could not find MURAM node\n");
			goto fman_node_put;
		}
		fman->dts_params.muram_phy_base_addr = phys_base_addr +
		    res.start;
		fman->dts_params.muram_size = res.end - res.start;
	}
#endif /* __rtems__ */
	{
		/* In B4 rev 2.0 (and above) the MURAM size is 512KB.
		 * Check the SVR and update MURAM size if required.
		 */
		u32 svr;

		svr = mfspr(SPRN_SVR);

		if ((SVR_SOC_VER(svr) == SVR_B4860) && (SVR_MAJ(svr) >= 2))
			fman->dts_params.muram_size = 0x80000;
	}

#ifndef __rtems__
	of_node_put(muram_node);
#endif /* __rtems__ */
	of_node_put(fm_node);

	err = devm_request_irq(&of_dev->dev, irq, fman_irq,
			       IRQF_NO_SUSPEND, "fman", fman);
	if (err < 0) {
		pr_err("Error: allocating irq %d (error = %d)\n", irq, err);
		goto fman_free;
	}

#ifndef __rtems__
	fman->dts_params.res =
		devm_request_mem_region(&of_dev->dev, phys_base_addr,
					mem_size, "fman");
	if (!fman->dts_params.res) {
		pr_err("request_mem_region() failed\n");
		goto fman_free;
	}

	fman->dts_params.base_addr =
		devm_ioremap(&of_dev->dev, phys_base_addr, mem_size);
	if (fman->dts_params.base_addr == 0) {
		pr_err("devm_ioremap() failed\n");
		goto fman_free;
	}
#endif /* __rtems__ */

	return fman;

fman_node_put:
	of_node_put(fm_node);
fman_free:
	kfree(fman);
	return NULL;
}

static irqreturn_t fman_err_irq(int irq, void *fman)
{
	if (fman_error_isr(fman) == 0)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static int fman_probe(struct platform_device *of_dev)
{
	struct fman *fman;
	struct device *dev;
	int err;

	dev = &of_dev->dev;

	fman = read_dts_node(of_dev);
	if (!fman)
		return -EIO;

	if (fman->dts_params.err_irq != 0) {
		err = devm_request_irq(dev, fman->dts_params.err_irq,
				       fman_err_irq,
				       IRQF_SHARED | IRQF_NO_SUSPEND,
				       "fman-err", fman);
		if (err < 0) {
			pr_err("Error: allocating irq %d (error = %d)\n",
			       fman->dts_params.err_irq, err);
			return -EINVAL;
		}
	}

	err = fman_config(fman);
	if (err) {
		pr_err("FMan config failed\n");
		return -EINVAL;
	}

	if (fman_init(fman) != 0) {
		pr_err("FMan init failed\n");
		return -EINVAL;
	}

	if (fman->dts_params.err_irq == 0) {
		fman_set_exception(fman, FMAN_EX_DMA_BUS_ERROR, false);
		fman_set_exception(fman, FMAN_EX_DMA_READ_ECC, false);
		fman_set_exception(fman, FMAN_EX_DMA_SYSTEM_WRITE_ECC, false);
		fman_set_exception(fman, FMAN_EX_DMA_FM_WRITE_ECC, false);
		fman_set_exception(fman, FMAN_EX_DMA_SINGLE_PORT_ECC, false);
		fman_set_exception(fman, FMAN_EX_FPM_STALL_ON_TASKS, false);
		fman_set_exception(fman, FMAN_EX_FPM_SINGLE_ECC, false);
		fman_set_exception(fman, FMAN_EX_FPM_DOUBLE_ECC, false);
		fman_set_exception(fman, FMAN_EX_QMI_SINGLE_ECC, false);
		fman_set_exception(fman, FMAN_EX_QMI_DOUBLE_ECC, false);
		fman_set_exception(fman,
				   FMAN_EX_QMI_DEQ_FROM_UNKNOWN_PORTID, false);
		fman_set_exception(fman, FMAN_EX_BMI_LIST_RAM_ECC, false);
		fman_set_exception(fman, FMAN_EX_BMI_STORAGE_PROFILE_ECC,
				   false);
		fman_set_exception(fman, FMAN_EX_BMI_STATISTICS_RAM_ECC, false);
		fman_set_exception(fman, FMAN_EX_BMI_DISPATCH_RAM_ECC, false);
	}

	dev_set_drvdata(dev, fman);

	fman->dev = dev;

	pr_debug("FM%d probed\n", fman->dts_params.id);

	return 0;
}

#ifndef __rtems__
static const struct of_device_id fman_match[] = {
	{
	 .compatible = "fsl,fman"},
	{}
};

MODULE_DEVICE_TABLE(of, fm_match);

static struct platform_driver fman_driver = {
	.driver = {
		   .name = "fsl-fman",
		   .of_match_table = fman_match,
		   },
	.probe = fman_probe,
};

builtin_platform_driver(fman_driver);
#else /* __rtems__ */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>

void
fman_reset(struct fman *fman)
{

	/*
	 * Ignore errata A007273, since we do not disable the Ethernet MAC
	 * clocks.
	 */

	out_be32(&fman->fpm_regs->fm_rstc, FPM_RSTC_FM_RESET);
	/* Memory barrier */
	mb();
	usleep_range(100, 300);

	if (!!(ioread32be(&fman->qmi_regs->fmqm_gs) &
	    QMI_GS_HALT_NOT_BUSY)) {
		usleep_range(100, 300);
	}
}

struct fman_softc {
	struct platform_device of_dev;
	struct device_node dn;
};

static int
fman_dev_probe_fdt(struct fman_softc *sc, int unit)
{
	const char *fdt = bsp_fdt_get();
	const char *name = "fsl,fman";
	int node = 0;

	while (1) {
		node = fdt_node_offset_by_compatible(fdt, node, name);
		if (node >= 0) {
			int len;
			const fdt32_t *p = fdt_getprop(fdt, node, "cell-index", &len);

			if (p != NULL && len == sizeof(*p)) {
				if (fdt32_to_cpu(*p) == (uint32_t)unit) {
					sc->dn.offset = node;
					sc->dn.full_name = name;
					sc->of_dev.dev.of_node = &sc->dn;
					sc->of_dev.dev.base = (uintptr_t)&qoriq.fman[unit];
					return (BUS_PROBE_DEFAULT);
				}
			} else {
				return (ENXIO);
			}
		} else {
			return (ENXIO);
		}
	}
}

static int
fman_dev_probe(device_t dev)
{
	struct fman_softc *sc = device_get_softc(dev);

	device_set_desc(dev, "FMan");

	return (fman_dev_probe_fdt(sc, device_get_unit(dev)));
}

static int
fman_dev_attach(device_t dev)
{
	const char *fdt = bsp_fdt_get();
	struct fman_softc *sc = device_get_softc(dev);
	int node;
	int err;

	err = fman_probe(&sc->of_dev);
	if (err != 0) {
		return (ENXIO);
	}

	node = fdt_first_subnode(fdt, sc->dn.offset);
	while (node >= 0) {
		struct fman_ivars *ivars =
		    kzalloc(sizeof(*ivars), GFP_KERNEL);
		device_t child;

		if (ivars == NULL) {
			return (ENOMEM);
		}

		ivars->dn.offset = node;
		ivars->of_dev.dev.of_node = &ivars->dn;
		ivars->of_dev.dev.base = sc->of_dev.dev.base;
		ivars->fman = dev_get_drvdata(&sc->of_dev.dev);

		child = device_add_child(dev, NULL, -1);
		if (child == NULL) {
			kfree(ivars);
			return (ENOMEM);
		}

		device_set_ivars(child, ivars);

		err = device_probe_and_attach(child);
		if (err != 0) {
			kfree(ivars);
		}

		node = fdt_next_subnode(fdt, node);
	}

	return (0);
}

static int
fman_dev_detach(device_t dev)
{
	struct fman_softc *sc = device_get_softc(dev);
	int err;

	err = bus_generic_detach(dev);
	if (err == 0) {
		fman_reset(dev_get_drvdata(&sc->of_dev.dev));
	}

	return (err);
}

static device_method_t fman_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, fman_dev_probe),
	DEVMETHOD(device_attach, fman_dev_attach),
	DEVMETHOD(device_detach, fman_dev_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

driver_t fman_driver = {
	.name = "fman",
	.methods = fman_methods,
	.size = sizeof(struct fman_softc),
};

static devclass_t fman_devclass;

DRIVER_MODULE(fman, nexus, fman_driver, fman_devclass, 0, 0);
#endif /* __rtems__ */
