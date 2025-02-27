/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_FPN_PARAM_H
#define __IA_CSS_FPN_PARAM_H

#include "type_support.h"

#include "dma.h"

#define FPN_BITS_PER_PIXEL	16

/* FPNR (Fixed Pattern Noise Reduction) */
struct sh_css_isp_fpn_params {
	s32 shift;
	s32 enabled;
};

struct sh_css_isp_fpn_isp_config {
	u32 width_a_over_b;
	struct dma_port_config port_b;
};

#endif /* __IA_CSS_FPN_PARAM_H */
