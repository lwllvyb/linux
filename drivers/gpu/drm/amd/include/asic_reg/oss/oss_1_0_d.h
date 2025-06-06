/*
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef OSS_1_0_D_H
#define OSS_1_0_D_H

#define ixCLIENT0_BM 0x0220
#define ixCLIENT0_CD0 0x0210
#define ixCLIENT0_CD1 0x0214
#define ixCLIENT0_CD2 0x0218
#define ixCLIENT0_CD3 0x021C
#define ixCLIENT0_CK0 0x0200
#define ixCLIENT0_CK1 0x0204
#define ixCLIENT0_CK2 0x0208
#define ixCLIENT0_CK3 0x020C
#define ixCLIENT0_K0 0x01F0
#define ixCLIENT0_K1 0x01F4
#define ixCLIENT0_K2 0x01F8
#define ixCLIENT0_K3 0x01FC
#define ixCLIENT0_OFFSET 0x0224
#define ixCLIENT0_OFFSET_HI 0x0290
#define ixCLIENT0_STATUS 0x0228
#define ixCLIENT1_BM 0x025C
#define ixCLIENT1_CD0 0x024C
#define ixCLIENT1_CD1 0x0250
#define ixCLIENT1_CD2 0x0254
#define ixCLIENT1_CD3 0x0258
#define ixCLIENT1_CK0 0x023C
#define ixCLIENT1_CK1 0x0240
#define ixCLIENT1_CK2 0x0244
#define ixCLIENT1_CK3 0x0248
#define ixCLIENT1_K0 0x022C
#define ixCLIENT1_K1 0x0230
#define ixCLIENT1_K2 0x0234
#define ixCLIENT1_K3 0x0238
#define ixCLIENT1_OFFSET 0x0260
#define ixCLIENT1_OFFSET_HI 0x0294
#define ixCLIENT1_PORT_STATUS 0x0264
#define ixCLIENT2_BM 0x01E4
#define ixCLIENT2_CD0 0x01D4
#define ixCLIENT2_CD1 0x01D8
#define ixCLIENT2_CD2 0x01DC
#define ixCLIENT2_CD3 0x01E0
#define ixCLIENT2_CK0 0x01C4
#define ixCLIENT2_CK1 0x01C8
#define ixCLIENT2_CK2 0x01CC
#define ixCLIENT2_CK3 0x01D0
#define ixCLIENT2_K0 0x01B4
#define ixCLIENT2_K1 0x01B8
#define ixCLIENT2_K2 0x01BC
#define ixCLIENT2_K3 0x01C0
#define ixCLIENT2_OFFSET 0x01E8
#define ixCLIENT2_OFFSET_HI 0x0298
#define ixCLIENT2_STATUS 0x01EC
#define ixCLIENT3_BM 0x02D4
#define ixCLIENT3_CD0 0x02C4
#define ixCLIENT3_CD1 0x02C8
#define ixCLIENT3_CD2 0x02CC
#define ixCLIENT3_CD3 0x02D0
#define ixCLIENT3_CK0 0x02B4
#define ixCLIENT3_CK1 0x02B8
#define ixCLIENT3_CK2 0x02BC
#define ixCLIENT3_CK3 0x02C0
#define ixCLIENT3_K0 0x02A4
#define ixCLIENT3_K1 0x02A8
#define ixCLIENT3_K2 0x02AC
#define ixCLIENT3_K3 0x02B0
#define ixCLIENT3_OFFSET 0x02D8
#define ixCLIENT3_OFFSET_HI 0x02A0
#define ixCLIENT3_STATUS 0x02DC
#define ixDH_TEST 0x0000
#define ixEXP0 0x0034
#define ixEXP1 0x0038
#define ixEXP2 0x003C
#define ixEXP3 0x0040
#define ixEXP4 0x0044
#define ixEXP5 0x0048
#define ixEXP6 0x004C
#define ixEXP7 0x0050
#define ixHFS_SEED0 0x0278
#define ixHFS_SEED1 0x027C
#define ixHFS_SEED2 0x0280
#define ixHFS_SEED3 0x0284
#define ixKEFUSE0 0x0268
#define ixKEFUSE1 0x026C
#define ixKEFUSE2 0x0270
#define ixKEFUSE3 0x0274
#define ixKHFS0 0x0004
#define ixKHFS1 0x0008
#define ixKHFS2 0x000C
#define ixKHFS3 0x0010
#define ixKSESSION0 0x0014
#define ixKSESSION1 0x0018
#define ixKSESSION2 0x001C
#define ixKSESSION3 0x0020
#define ixKSIG0 0x0024
#define ixKSIG1 0x0028
#define ixKSIG2 0x002C
#define ixKSIG3 0x0030
#define ixLX0 0x0054
#define ixLX1 0x0058
#define ixLX2 0x005C
#define ixLX3 0x0060
#define ixRINGOSC_MASK 0x0288
#define ixSPU_PORT_STATUS 0x029C
#define mmCC_DRM_ID_STRAPS 0x1559
#define mmCC_SYS_RB_BACKEND_DISABLE 0x03A0
#define mmCC_SYS_RB_REDUNDANCY 0x039F
#define mmCGTT_DRM_CLK_CTRL0 0x1579
#define mmCP_CONFIG 0x0F92
#define mmDC_TEST_DEBUG_DATA 0x157D
#define mmDC_TEST_DEBUG_INDEX 0x157C
#define mmGC_USER_SYS_RB_BACKEND_DISABLE 0x03A1
#define mmHDP_ADDR_CONFIG 0x0BD2
#define mmHDP_DEBUG0 0x0BCC
#define mmHDP_DEBUG1 0x0BCD
#define mmHDP_HOST_PATH_CNTL 0x0B00
#define mmHDP_LAST_SURFACE_HIT 0x0BCE
#define mmHDP_MEMIO_ADDR 0x0BF7
#define mmHDP_MEMIO_CNTL 0x0BF6
#define mmHDP_MEMIO_RD_DATA 0x0BFA
#define mmHDP_MEMIO_STATUS 0x0BF8
#define mmHDP_MEMIO_WR_DATA 0x0BF9
#define mmHDP_MEM_POWER_LS 0x0BD4
#define mmHDP_MISC_CNTL 0x0BD3
#define mmHDP_NONSURFACE_BASE 0x0B01
#define mmHDP_NONSURFACE_INFO 0x0B02
#define mmHDP_NONSURFACE_PREFETCH 0x0BD5
#define mmHDP_NONSURFACE_SIZE 0x0B03
#define mmHDP_NONSURF_FLAGS 0x0BC9
#define mmHDP_NONSURF_FLAGS_CLR 0x0BCA
#define mmHDP_OUTSTANDING_REQ 0x0BD1
#define mmHDP_SC_MULTI_CHIP_CNTL 0x0BD0
#define mmHDP_SW_SEMAPHORE 0x0BCB
#define mmHDP_TILING_CONFIG 0x0BCF
#define mmHDP_XDP_BARS_ADDR_39_36 0x0C44
#define mmHDP_XDP_BUSY_STS 0x0C3E
#define mmHDP_XDP_CGTT_BLK_CTRL 0x0C33
#define mmHDP_XDP_CHKN 0x0C40
#define mmHDP_XDP_D2H_BAR_UPDATE 0x0C02
#define mmHDP_XDP_D2H_FLUSH 0x0C01
#define mmHDP_XDP_D2H_RSVD_10 0x0C0A
#define mmHDP_XDP_D2H_RSVD_11 0x0C0B
#define mmHDP_XDP_D2H_RSVD_12 0x0C0C
#define mmHDP_XDP_D2H_RSVD_13 0x0C0D
#define mmHDP_XDP_D2H_RSVD_14 0x0C0E
#define mmHDP_XDP_D2H_RSVD_15 0x0C0F
#define mmHDP_XDP_D2H_RSVD_16 0x0C10
#define mmHDP_XDP_D2H_RSVD_17 0x0C11
#define mmHDP_XDP_D2H_RSVD_18 0x0C12
#define mmHDP_XDP_D2H_RSVD_19 0x0C13
#define mmHDP_XDP_D2H_RSVD_20 0x0C14
#define mmHDP_XDP_D2H_RSVD_21 0x0C15
#define mmHDP_XDP_D2H_RSVD_22 0x0C16
#define mmHDP_XDP_D2H_RSVD_23 0x0C17
#define mmHDP_XDP_D2H_RSVD_24 0x0C18
#define mmHDP_XDP_D2H_RSVD_25 0x0C19
#define mmHDP_XDP_D2H_RSVD_26 0x0C1A
#define mmHDP_XDP_D2H_RSVD_27 0x0C1B
#define mmHDP_XDP_D2H_RSVD_28 0x0C1C
#define mmHDP_XDP_D2H_RSVD_29 0x0C1D
#define mmHDP_XDP_D2H_RSVD_30 0x0C1E
#define mmHDP_XDP_D2H_RSVD_3 0x0C03
#define mmHDP_XDP_D2H_RSVD_31 0x0C1F
#define mmHDP_XDP_D2H_RSVD_32 0x0C20
#define mmHDP_XDP_D2H_RSVD_33 0x0C21
#define mmHDP_XDP_D2H_RSVD_34 0x0C22
#define mmHDP_XDP_D2H_RSVD_4 0x0C04
#define mmHDP_XDP_D2H_RSVD_5 0x0C05
#define mmHDP_XDP_D2H_RSVD_6 0x0C06
#define mmHDP_XDP_D2H_RSVD_7 0x0C07
#define mmHDP_XDP_D2H_RSVD_8 0x0C08
#define mmHDP_XDP_D2H_RSVD_9 0x0C09
#define mmHDP_XDP_DBG_ADDR 0x0C41
#define mmHDP_XDP_DBG_DATA 0x0C42
#define mmHDP_XDP_DBG_MASK 0x0C43
#define mmHDP_XDP_DIRECT2HDP_FIRST 0x0C00
#define mmHDP_XDP_DIRECT2HDP_LAST 0x0C23
#define mmHDP_XDP_FLUSH_ARMED_STS 0x0C3C
#define mmHDP_XDP_FLUSH_CNTR0_STS 0x0C3D
#define mmHDP_XDP_HDP_IPH_CFG 0x0C31
#define mmHDP_XDP_HDP_MBX_MC_CFG 0x0C2D
#define mmHDP_XDP_HDP_MC_CFG 0x0C2E
#define mmHDP_XDP_HST_CFG 0x0C2F
#define mmHDP_XDP_P2P_BAR0 0x0C34
#define mmHDP_XDP_P2P_BAR1 0x0C35
#define mmHDP_XDP_P2P_BAR2 0x0C36
#define mmHDP_XDP_P2P_BAR3 0x0C37
#define mmHDP_XDP_P2P_BAR4 0x0C38
#define mmHDP_XDP_P2P_BAR5 0x0C39
#define mmHDP_XDP_P2P_BAR6 0x0C3A
#define mmHDP_XDP_P2P_BAR7 0x0C3B
#define mmHDP_XDP_P2P_BAR_CFG 0x0C24
#define mmHDP_XDP_P2P_MBX_ADDR0 0x0C26
#define mmHDP_XDP_P2P_MBX_ADDR1 0x0C27
#define mmHDP_XDP_P2P_MBX_ADDR2 0x0C28
#define mmHDP_XDP_P2P_MBX_ADDR3 0x0C29
#define mmHDP_XDP_P2P_MBX_ADDR4 0x0C2A
#define mmHDP_XDP_P2P_MBX_ADDR5 0x0C2B
#define mmHDP_XDP_P2P_MBX_ADDR6 0x0C2C
#define mmHDP_XDP_P2P_MBX_OFFSET 0x0C25
#define mmHDP_XDP_SID_CFG 0x0C30
#define mmHDP_XDP_SRBM_CFG 0x0C32
#define mmHDP_XDP_STICKY 0x0C3F
#define mmIH_ADVFAULT_CNTL 0x0F8C
#define mmIH_CNTL 0x0F86
#define mmIH_LEVEL_STATUS 0x0F87
#define mmIH_PERFCOUNTER0_RESULT 0x0F8A
#define mmIH_PERFCOUNTER1_RESULT 0x0F8B
#define mmIH_PERFMON_CNTL 0x0F89
#define mmIH_RB_BASE 0x0F81
#define mmIH_RB_CNTL 0x0F80
#define mmIH_RB_RPTR 0x0F82
#define mmIH_RB_WPTR 0x0F83
#define mmIH_RB_WPTR_ADDR_HI 0x0F84
#define mmIH_RB_WPTR_ADDR_LO 0x0F85
#define mmIH_STATUS 0x0F88

#define mmDMA_GFX_RB_CNTL                                       0x3400
#define mmDMA_GFX_RB_BASE                                       0x3401
#define mmDMA_GFX_RB_RPTR                                       0x3402
#define mmDMA_GFX_RB_WPTR                                       0x3403
#define mmDMA_GFX_RB_RPTR_ADDR_HI                               0x3407
#define mmDMA_GFX_RB_RPTR_ADDR_LO                               0x3408
#define mmDMA_GFX_IB_CNTL                                       0x3409
#define mmDMA_GFX_IB_RPTR                                       0x340a
#define mmDMA_CNTL                                          0x340b
#define mmDMA_STATUS_REG                                    0x340D
#define mmDMA_TILING_CONFIG  				  0x342E
#define mmDMA_SEM_INCOMPLETE_TIMER_CNTL                     0x3411
#define mmDMA_SEM_WAIT_FAIL_TIMER_CNTL                      0x3412
#define mmDMA_POWER_CNTL					0x342F
#define mmDMA_CLK_CTRL					0x3430
#define mmDMA_PG						0x3435
#define mmDMA_PGFSM_CONFIG				0x3436
#define mmDMA_PGFSM_WRITE					0x3437

#define mmSEM_MAILBOX 0x0F9B
#define mmSEM_MAILBOX_CLIENTCONFIG 0x0F9A
#define mmSEM_MAILBOX_CONTROL 0x0F9C
#define mmSEM_MCIF_CONFIG 0x0F90
#define mmSRBM_CAM_DATA 0x0397
#define mmSRBM_CAM_INDEX 0x0396
#define mmSRBM_CHIP_REVISION 0x039B
#define mmSRBM_CNTL 0x0390
#define mmSRBM_DEBUG 0x03A4
#define mmSRBM_DEBUG_CNTL 0x0399
#define mmSRBM_DEBUG_DATA 0x039A
#define mmSRBM_DEBUG_SNAPSHOT 0x03A5
#define mmSRBM_GFX_CNTL 0x0391
#define mmSRBM_INT_ACK 0x03AA
#define mmSRBM_INT_CNTL 0x03A8
#define mmSRBM_INT_STATUS 0x03A9
#define mmSRBM_MC_CLKEN_CNTL 0x03B3
#define mmSRBM_PERFCOUNTER0_HI 0x0704
#define mmSRBM_PERFCOUNTER0_LO 0x0703
#define mmSRBM_PERFCOUNTER0_SELECT 0x0701
#define mmSRBM_PERFCOUNTER1_HI 0x0706
#define mmSRBM_PERFCOUNTER1_LO 0x0705
#define mmSRBM_PERFCOUNTER1_SELECT 0x0702
#define mmSRBM_PERFMON_CNTL 0x0700
#define mmSRBM_READ_ERROR 0x03A6
#define mmSRBM_SOFT_RESET 0x0398
#define mmSRBM_STATUS 0x0394
#define mmSRBM_STATUS2 0x0393
#define mmSRBM_SYS_CLKEN_CNTL 0x03B4
#define mmSRBM_UVD_CLKEN_CNTL 0x03B6
#define mmSRBM_VCE_CLKEN_CNTL 0x03B5
#define mmUVD_CONFIG 0x0F98
#define mmVCE_CONFIG 0x0F94
#define mmXDMA_MSTR_MEM_OVERFLOW_CNTL 0x03F8

#endif
