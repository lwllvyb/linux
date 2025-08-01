// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2015 Glider bvba
 *
 * Based on clk-mstp.c, clk-rcar-gen2.c, and clk-rcar-gen3.c
 *
 * Copyright (C) 2013 Ideas On Board SPRL
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/psci.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/string_choices.h>

#include <dt-bindings/clock/renesas-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "clk-div6.h"

#ifdef DEBUG
#define WARN_DEBUG(x)	WARN_ON(x)
#else
#define WARN_DEBUG(x)	do { } while (0)
#endif

/*
 * Module Standby and Software Reset register offets.
 *
 * If the registers exist, these are valid for SH-Mobile, R-Mobile,
 * R-Car Gen2, R-Car Gen3, and RZ/G1.
 * These are NOT valid for R-Car Gen1 and RZ/A1!
 */

/*
 * Module Stop Status Register offsets
 */

static const u16 mstpsr[] = {
	0x030, 0x038, 0x040, 0x048, 0x04C, 0x03C, 0x1C0, 0x1C4,
	0x9A0, 0x9A4, 0x9A8, 0x9AC,
};

static const u16 mstpsr_for_gen4[] = {
	0x2E00, 0x2E04, 0x2E08, 0x2E0C, 0x2E10, 0x2E14, 0x2E18, 0x2E1C,
	0x2E20, 0x2E24, 0x2E28, 0x2E2C, 0x2E30, 0x2E34, 0x2E38, 0x2E3C,
	0x2E40, 0x2E44, 0x2E48, 0x2E4C, 0x2E50, 0x2E54, 0x2E58, 0x2E5C,
	0x2E60, 0x2E64, 0x2E68, 0x2E6C, 0x2E70, 0x2E74,
};

/*
 * System Module Stop Control Register offsets
 */

static const u16 smstpcr[] = {
	0x130, 0x134, 0x138, 0x13C, 0x140, 0x144, 0x148, 0x14C,
	0x990, 0x994, 0x998, 0x99C,
};

static const u16 mstpcr_for_gen4[] = {
	0x2D00, 0x2D04, 0x2D08, 0x2D0C, 0x2D10, 0x2D14, 0x2D18, 0x2D1C,
	0x2D20, 0x2D24, 0x2D28, 0x2D2C, 0x2D30, 0x2D34, 0x2D38, 0x2D3C,
	0x2D40, 0x2D44, 0x2D48, 0x2D4C, 0x2D50, 0x2D54, 0x2D58, 0x2D5C,
	0x2D60, 0x2D64, 0x2D68, 0x2D6C, 0x2D70, 0x2D74,
};

/*
 * Module Stop Control Register (RZ/T2H)
 * RZ/T2H has 2 registers blocks,
 * Bit 12 is used to differentiate them
 */

#define RZT2H_MSTPCR_BLOCK_SHIFT	12
#define RZT2H_MSTPCR_OFFSET_MASK	GENMASK(11, 0)
#define RZT2H_MSTPCR(block, offset)	(((block) << RZT2H_MSTPCR_BLOCK_SHIFT) | \
					((offset) & RZT2H_MSTPCR_OFFSET_MASK))

#define RZT2H_MSTPCR_BLOCK(x)		((x) >> RZT2H_MSTPCR_BLOCK_SHIFT)
#define RZT2H_MSTPCR_OFFSET(x)		((x) & RZT2H_MSTPCR_OFFSET_MASK)

static const u16 mstpcr_for_rzt2h[] = {
	RZT2H_MSTPCR(0, 0x300), /* MSTPCRA */
	RZT2H_MSTPCR(0, 0x304), /* MSTPCRB */
	RZT2H_MSTPCR(0, 0x308), /* MSTPCRC */
	RZT2H_MSTPCR(0, 0x30c),	/* MSTPCRD */
	RZT2H_MSTPCR(0, 0x310), /* MSTPCRE */
	0,
	RZT2H_MSTPCR(1, 0x318), /* MSTPCRG */
	0,
	RZT2H_MSTPCR(1, 0x320), /* MSTPCRI */
	RZT2H_MSTPCR(0, 0x324), /* MSTPCRJ */
	RZT2H_MSTPCR(0, 0x328), /* MSTPCRK */
	RZT2H_MSTPCR(0, 0x32c), /* MSTPCRL */
	RZT2H_MSTPCR(0, 0x330), /* MSTPCRM */
	RZT2H_MSTPCR(1, 0x334), /* MSTPCRN */
};

/*
 * Standby Control Register offsets (RZ/A)
 * Base address is FRQCR register
 */

static const u16 stbcr[] = {
	0xFFFF/*dummy*/, 0x010, 0x014, 0x410, 0x414, 0x418, 0x41C, 0x420,
	0x424, 0x428, 0x42C,
};

/*
 * Software Reset Register offsets
 */

static const u16 srcr[] = {
	0x0A0, 0x0A8, 0x0B0, 0x0B8, 0x0BC, 0x0C4, 0x1C8, 0x1CC,
	0x920, 0x924, 0x928, 0x92C,
};

static const u16 srcr_for_gen4[] = {
	0x2C00, 0x2C04, 0x2C08, 0x2C0C, 0x2C10, 0x2C14, 0x2C18, 0x2C1C,
	0x2C20, 0x2C24, 0x2C28, 0x2C2C, 0x2C30, 0x2C34, 0x2C38, 0x2C3C,
	0x2C40, 0x2C44, 0x2C48, 0x2C4C, 0x2C50, 0x2C54, 0x2C58, 0x2C5C,
	0x2C60, 0x2C64, 0x2C68, 0x2C6C, 0x2C70, 0x2C74,
};

/*
 * Software Reset Clearing Register offsets
 */

static const u16 srstclr[] = {
	0x940, 0x944, 0x948, 0x94C, 0x950, 0x954, 0x958, 0x95C,
	0x960, 0x964, 0x968, 0x96C,
};

static const u16 srstclr_for_gen4[] = {
	0x2C80, 0x2C84, 0x2C88, 0x2C8C, 0x2C90, 0x2C94, 0x2C98, 0x2C9C,
	0x2CA0, 0x2CA4, 0x2CA8, 0x2CAC, 0x2CB0, 0x2CB4, 0x2CB8, 0x2CBC,
	0x2CC0, 0x2CC4, 0x2CC8, 0x2CCC, 0x2CD0, 0x2CD4, 0x2CD8, 0x2CDC,
	0x2CE0, 0x2CE4, 0x2CE8, 0x2CEC, 0x2CF0, 0x2CF4,
};

/**
 * struct cpg_mssr_priv - Clock Pulse Generator / Module Standby
 *                        and Software Reset Private Data
 *
 * @pub: Data passed to clock registration callback
 * @rcdev: Optional reset controller entity
 * @dev: CPG/MSSR device
 * @reg_layout: CPG/MSSR register layout
 * @np: Device node in DT for this CPG/MSSR module
 * @num_core_clks: Number of Core Clocks in clks[]
 * @num_mod_clks: Number of Module Clocks in clks[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @status_regs: Pointer to status registers array
 * @control_regs: Pointer to control registers array
 * @reset_regs: Pointer to reset registers array
 * @reset_clear_regs:  Pointer to reset clearing registers array
 * @smstpcr_saved: [].mask: Mask of SMSTPCR[] bits under our control
 *                 [].val: Saved values of SMSTPCR[]
 * @reserved_ids: Temporary used, reserved id list
 * @num_reserved_ids: Temporary used, number of reserved id list
 * @clks: Array containing all Core and Module Clocks
 */
struct cpg_mssr_priv {
	struct cpg_mssr_pub pub;
#ifdef CONFIG_RESET_CONTROLLER
	struct reset_controller_dev rcdev;
#endif
	struct device *dev;
	enum clk_reg_layout reg_layout;
	struct device_node *np;

	unsigned int num_core_clks;
	unsigned int num_mod_clks;
	unsigned int last_dt_core_clk;

	const u16 *status_regs;
	const u16 *control_regs;
	const u16 *reset_regs;
	const u16 *reset_clear_regs;
	struct {
		u32 mask;
		u32 val;
	} smstpcr_saved[ARRAY_SIZE(mstpsr_for_gen4)];

	unsigned int *reserved_ids;
	unsigned int num_reserved_ids;

	struct clk *clks[];
};

static struct cpg_mssr_priv *cpg_mssr_priv;

/**
 * struct mstp_clock - MSTP gating clock
 * @hw: handle between common and hardware-specific interfaces
 * @index: MSTP clock number
 * @priv: CPG/MSSR private data
 */
struct mstp_clock {
	struct clk_hw hw;
	u32 index;
	struct cpg_mssr_priv *priv;
};

#define to_mstp_clock(_hw) container_of(_hw, struct mstp_clock, hw)

static u32 cpg_rzt2h_mstp_read(struct clk_hw *hw, u16 offset)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	void __iomem *base =
		RZT2H_MSTPCR_BLOCK(offset) ? priv->pub.base1 : priv->pub.base0;

	return readl(base + RZT2H_MSTPCR_OFFSET(offset));
}

static void cpg_rzt2h_mstp_write(struct clk_hw *hw, u16 offset, u32 value)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	void __iomem *base =
		RZT2H_MSTPCR_BLOCK(offset) ? priv->pub.base1 : priv->pub.base0;

	writel(value, base + RZT2H_MSTPCR_OFFSET(offset));
}

static int cpg_mstp_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	unsigned int reg = clock->index / 32;
	unsigned int bit = clock->index % 32;
	struct device *dev = priv->dev;
	u32 bitmask = BIT(bit);
	unsigned long flags;
	u32 value;
	int error;

	dev_dbg(dev, "MSTP %u%02u/%pC %s\n", reg, bit, hw->clk,
		str_on_off(enable));
	spin_lock_irqsave(&priv->pub.rmw_lock, flags);

	if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A) {
		value = readb(priv->pub.base0 + priv->control_regs[reg]);
		if (enable)
			value &= ~bitmask;
		else
			value |= bitmask;
		writeb(value, priv->pub.base0 + priv->control_regs[reg]);

		/* dummy read to ensure write has completed */
		readb(priv->pub.base0 + priv->control_regs[reg]);
		barrier_data(priv->pub.base0 + priv->control_regs[reg]);

	} else if (priv->reg_layout == CLK_REG_LAYOUT_RZ_T2H) {
		value = cpg_rzt2h_mstp_read(hw,
					    priv->control_regs[reg]);

		if (enable)
			value &= ~bitmask;
		else
			value |= bitmask;

		cpg_rzt2h_mstp_write(hw,
				     priv->control_regs[reg],
				     value);
	} else {
		value = readl(priv->pub.base0 + priv->control_regs[reg]);
		if (enable)
			value &= ~bitmask;
		else
			value |= bitmask;
		writel(value, priv->pub.base0 + priv->control_regs[reg]);
	}

	spin_unlock_irqrestore(&priv->pub.rmw_lock, flags);

	if (!enable || priv->reg_layout == CLK_REG_LAYOUT_RZ_A ||
	    priv->reg_layout == CLK_REG_LAYOUT_RZ_T2H)
		return 0;

	error = readl_poll_timeout_atomic(priv->pub.base0 + priv->status_regs[reg],
					  value, !(value & bitmask), 0, 10);
	if (error)
		dev_err(dev, "Failed to enable SMSTP %p[%d]\n",
			priv->pub.base0 + priv->control_regs[reg], bit);

	return error;
}

static int cpg_mstp_clock_enable(struct clk_hw *hw)
{
	return cpg_mstp_clock_endisable(hw, true);
}

static void cpg_mstp_clock_disable(struct clk_hw *hw)
{
	cpg_mstp_clock_endisable(hw, false);
}

static int cpg_mstp_clock_is_enabled(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	unsigned int reg = clock->index / 32;
	u32 value;

	if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A)
		value = readb(priv->pub.base0 + priv->control_regs[reg]);
	else if (priv->reg_layout == CLK_REG_LAYOUT_RZ_T2H)
		value = cpg_rzt2h_mstp_read(hw,
					    priv->control_regs[reg]);
	else
		value = readl(priv->pub.base0 + priv->status_regs[reg]);

	return !(value & BIT(clock->index % 32));
}

static const struct clk_ops cpg_mstp_clock_ops = {
	.enable = cpg_mstp_clock_enable,
	.disable = cpg_mstp_clock_disable,
	.is_enabled = cpg_mstp_clock_is_enabled,
};

static
struct clk *cpg_mssr_clk_src_twocell_get(struct of_phandle_args *clkspec,
					 void *data)
{
	unsigned int clkidx = clkspec->args[1];
	struct cpg_mssr_priv *priv = data;
	struct device *dev = priv->dev;
	unsigned int idx;
	const char *type;
	struct clk *clk;
	int range_check;

	switch (clkspec->args[0]) {
	case CPG_CORE:
		type = "core";
		if (clkidx > priv->last_dt_core_clk) {
			dev_err(dev, "Invalid %s clock index %u\n", type,
			       clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[clkidx];
		break;

	case CPG_MOD:
		type = "module";
		if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A) {
			idx = MOD_CLK_PACK_10(clkidx);
			range_check = 7 - (clkidx % 10);
		} else {
			idx = MOD_CLK_PACK(clkidx);
			range_check = 31 - (clkidx % 100);
		}
		if (range_check < 0 || idx >= priv->num_mod_clks) {
			dev_err(dev, "Invalid %s clock index %u\n", type,
				clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[priv->num_core_clks + idx];
		break;

	default:
		dev_err(dev, "Invalid CPG clock type %u\n", clkspec->args[0]);
		return ERR_PTR(-EINVAL);
	}

	if (IS_ERR(clk))
		dev_err(dev, "Cannot get %s clock %u: %ld", type, clkidx,
		       PTR_ERR(clk));
	else
		dev_dbg(dev, "clock (%u, %u) is %pC at %lu Hz\n",
			clkspec->args[0], clkspec->args[1], clk,
			clk_get_rate(clk));
	return clk;
}

static void __init cpg_mssr_register_core_clk(const struct cpg_core_clk *core,
					      const struct cpg_mssr_info *info,
					      struct cpg_mssr_priv *priv)
{
	struct clk *clk = ERR_PTR(-ENOTSUPP), *parent;
	struct device *dev = priv->dev;
	unsigned int id = core->id, div = core->div;
	const char *parent_name;

	WARN_DEBUG(id >= priv->num_core_clks);
	WARN_DEBUG(PTR_ERR(priv->clks[id]) != -ENOENT);

	switch (core->type) {
	case CLK_TYPE_IN:
		clk = of_clk_get_by_name(priv->np, core->name);
		break;

	case CLK_TYPE_FF:
	case CLK_TYPE_DIV6P1:
	case CLK_TYPE_DIV6_RO:
		WARN_DEBUG(core->parent >= priv->num_core_clks);
		parent = priv->pub.clks[core->parent];
		if (IS_ERR(parent)) {
			clk = parent;
			goto fail;
		}

		parent_name = __clk_get_name(parent);

		if (core->type == CLK_TYPE_DIV6_RO)
			/* Multiply with the DIV6 register value */
			div *= (readl(priv->pub.base0 + core->offset) & 0x3f) + 1;

		if (core->type == CLK_TYPE_DIV6P1) {
			clk = cpg_div6_register(core->name, 1, &parent_name,
						priv->pub.base0 + core->offset,
						&priv->pub.notifiers);
		} else {
			clk = clk_register_fixed_factor(NULL, core->name,
							parent_name, 0,
							core->mult, div);
		}
		break;

	case CLK_TYPE_FR:
		clk = clk_register_fixed_rate(NULL, core->name, NULL, 0,
					      core->mult);
		break;

	default:
		if (info->cpg_clk_register)
			clk = info->cpg_clk_register(dev, core, info,
						     &priv->pub);
		else
			dev_err(dev, "%s has unsupported core clock type %u\n",
				core->name, core->type);
		break;
	}

	if (IS_ERR_OR_NULL(clk))
		goto fail;

	dev_dbg(dev, "Core clock %pC at %lu Hz\n", clk, clk_get_rate(clk));
	priv->pub.clks[id] = clk;
	return;

fail:
	dev_err(dev, "Failed to register %s clock %s: %ld\n", "core",
		core->name, PTR_ERR(clk));
}

static void __init cpg_mssr_register_mod_clk(const struct mssr_mod_clk *mod,
					     const struct cpg_mssr_info *info,
					     struct cpg_mssr_priv *priv)
{
	struct mstp_clock *clock = NULL;
	struct device *dev = priv->dev;
	unsigned int id = mod->id;
	struct clk_init_data init = {};
	struct clk *parent, *clk;
	const char *parent_name;
	unsigned int i;

	WARN_DEBUG(id < priv->num_core_clks);
	WARN_DEBUG(id >= priv->num_core_clks + priv->num_mod_clks);
	WARN_DEBUG(mod->parent >= priv->num_core_clks + priv->num_mod_clks);
	WARN_DEBUG(PTR_ERR(priv->pub.clks[id]) != -ENOENT);

	if (!mod->name) {
		/* Skip NULLified clock */
		return;
	}

	parent = priv->pub.clks[mod->parent];
	if (IS_ERR(parent)) {
		clk = parent;
		goto fail;
	}

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		clk = ERR_PTR(-ENOMEM);
		goto fail;
	}

	init.name = mod->name;
	init.ops = &cpg_mstp_clock_ops;
	init.flags = CLK_SET_RATE_PARENT;
	parent_name = __clk_get_name(parent);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->index = id - priv->num_core_clks;
	clock->priv = priv;
	clock->hw.init = &init;

	for (i = 0; i < info->num_crit_mod_clks; i++)
		if (id == info->crit_mod_clks[i] &&
		    cpg_mstp_clock_is_enabled(&clock->hw)) {
			dev_dbg(dev, "MSTP %s setting CLK_IS_CRITICAL\n",
				mod->name);
			init.flags |= CLK_IS_CRITICAL;
			break;
		}

	/*
	 * Ignore reserved device.
	 * see
	 *	cpg_mssr_reserved_init()
	 */
	for (i = 0; i < priv->num_reserved_ids; i++) {
		if (id == priv->reserved_ids[i]) {
			dev_info(dev, "Ignore Linux non-assigned mod (%s)\n", mod->name);
			init.flags |= CLK_IGNORE_UNUSED;
			break;
		}
	}

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto fail;

	dev_dbg(dev, "Module clock %pC at %lu Hz\n", clk, clk_get_rate(clk));
	priv->clks[id] = clk;
	priv->smstpcr_saved[clock->index / 32].mask |= BIT(clock->index % 32);
	return;

fail:
	dev_err(dev, "Failed to register %s clock %s: %ld\n", "module",
		mod->name, PTR_ERR(clk));
	kfree(clock);
}

struct cpg_mssr_clk_domain {
	struct generic_pm_domain genpd;
	unsigned int num_core_pm_clks;
	unsigned int core_pm_clks[];
};

static struct cpg_mssr_clk_domain *cpg_mssr_clk_domain;

static bool cpg_mssr_is_pm_clk(const struct of_phandle_args *clkspec,
			       struct cpg_mssr_clk_domain *pd)
{
	unsigned int i;

	if (clkspec->np != pd->genpd.dev.of_node || clkspec->args_count != 2)
		return false;

	switch (clkspec->args[0]) {
	case CPG_CORE:
		for (i = 0; i < pd->num_core_pm_clks; i++)
			if (clkspec->args[1] == pd->core_pm_clks[i])
				return true;
		return false;

	case CPG_MOD:
		return true;

	default:
		return false;
	}
}

int cpg_mssr_attach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	struct cpg_mssr_clk_domain *pd = cpg_mssr_clk_domain;
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	struct clk *clk;
	int i = 0;
	int error;

	if (!pd) {
		dev_dbg(dev, "CPG/MSSR clock domain not yet available\n");
		return -EPROBE_DEFER;
	}

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
					   &clkspec)) {
		if (cpg_mssr_is_pm_clk(&clkspec, pd))
			goto found;

		of_node_put(clkspec.np);
		i++;
	}

	return 0;

found:
	clk = of_clk_get_from_provider(&clkspec);
	of_node_put(clkspec.np);

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	error = pm_clk_create(dev);
	if (error)
		goto fail_put;

	error = pm_clk_add_clk(dev, clk);
	if (error)
		goto fail_destroy;

	return 0;

fail_destroy:
	pm_clk_destroy(dev);
fail_put:
	clk_put(clk);
	return error;
}

void cpg_mssr_detach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	if (!pm_clk_no_clocks(dev))
		pm_clk_destroy(dev);
}

static void cpg_mssr_genpd_remove(void *data)
{
	pm_genpd_remove(data);
}

static int __init cpg_mssr_add_clk_domain(struct device *dev,
					  const unsigned int *core_pm_clks,
					  unsigned int num_core_pm_clks)
{
	struct device_node *np = dev->of_node;
	struct generic_pm_domain *genpd;
	struct cpg_mssr_clk_domain *pd;
	size_t pm_size = num_core_pm_clks * sizeof(core_pm_clks[0]);
	int ret;

	pd = devm_kzalloc(dev, sizeof(*pd) + pm_size, GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->num_core_pm_clks = num_core_pm_clks;
	memcpy(pd->core_pm_clks, core_pm_clks, pm_size);

	genpd = &pd->genpd;
	genpd->name = np->name;
	genpd->flags = GENPD_FLAG_PM_CLK | GENPD_FLAG_ALWAYS_ON |
		       GENPD_FLAG_ACTIVE_WAKEUP;
	genpd->attach_dev = cpg_mssr_attach_dev;
	genpd->detach_dev = cpg_mssr_detach_dev;
	ret = pm_genpd_init(genpd, &pm_domain_always_on_gov, false);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, cpg_mssr_genpd_remove, genpd);
	if (ret)
		return ret;

	cpg_mssr_clk_domain = pd;

	return of_genpd_add_provider_simple(np, genpd);
}

#ifdef CONFIG_RESET_CONTROLLER

#define rcdev_to_priv(x)	container_of(x, struct cpg_mssr_priv, rcdev)

static int cpg_mssr_reset(struct reset_controller_dev *rcdev,
			  unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int reg = id / 32;
	unsigned int bit = id % 32;
	u32 bitmask = BIT(bit);

	dev_dbg(priv->dev, "reset %u%02u\n", reg, bit);

	/* Reset module */
	writel(bitmask, priv->pub.base0 + priv->reset_regs[reg]);

	/* Wait for at least one cycle of the RCLK clock (@ ca. 32 kHz) */
	udelay(35);

	/* Release module from reset state */
	writel(bitmask, priv->pub.base0 + priv->reset_clear_regs[reg]);

	return 0;
}

static int cpg_mssr_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int reg = id / 32;
	unsigned int bit = id % 32;
	u32 bitmask = BIT(bit);

	dev_dbg(priv->dev, "assert %u%02u\n", reg, bit);

	writel(bitmask, priv->pub.base0 + priv->reset_regs[reg]);
	return 0;
}

static int cpg_mssr_deassert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int reg = id / 32;
	unsigned int bit = id % 32;
	u32 bitmask = BIT(bit);

	dev_dbg(priv->dev, "deassert %u%02u\n", reg, bit);

	writel(bitmask, priv->pub.base0 + priv->reset_clear_regs[reg]);
	return 0;
}

static int cpg_mssr_status(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int reg = id / 32;
	unsigned int bit = id % 32;
	u32 bitmask = BIT(bit);

	return !!(readl(priv->pub.base0 + priv->reset_regs[reg]) & bitmask);
}

static const struct reset_control_ops cpg_mssr_reset_ops = {
	.reset = cpg_mssr_reset,
	.assert = cpg_mssr_assert,
	.deassert = cpg_mssr_deassert,
	.status = cpg_mssr_status,
};

static int cpg_mssr_reset_xlate(struct reset_controller_dev *rcdev,
				const struct of_phandle_args *reset_spec)
{
	struct cpg_mssr_priv *priv = rcdev_to_priv(rcdev);
	unsigned int unpacked = reset_spec->args[0];
	unsigned int idx = MOD_CLK_PACK(unpacked);

	if (unpacked % 100 > 31 || idx >= rcdev->nr_resets) {
		dev_err(priv->dev, "Invalid reset index %u\n", unpacked);
		return -EINVAL;
	}

	return idx;
}

static int cpg_mssr_reset_controller_register(struct cpg_mssr_priv *priv)
{
	priv->rcdev.ops = &cpg_mssr_reset_ops;
	priv->rcdev.of_node = priv->dev->of_node;
	priv->rcdev.of_reset_n_cells = 1;
	priv->rcdev.of_xlate = cpg_mssr_reset_xlate;
	priv->rcdev.nr_resets = priv->num_mod_clks;
	return devm_reset_controller_register(priv->dev, &priv->rcdev);
}

#else /* !CONFIG_RESET_CONTROLLER */
static inline int cpg_mssr_reset_controller_register(struct cpg_mssr_priv *priv)
{
	return 0;
}
#endif /* !CONFIG_RESET_CONTROLLER */

static const struct of_device_id cpg_mssr_match[] = {
#ifdef CONFIG_CLK_R7S9210
	{
		.compatible = "renesas,r7s9210-cpg-mssr",
		.data = &r7s9210_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7742
	{
		.compatible = "renesas,r8a7742-cpg-mssr",
		.data = &r8a7742_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7743
	{
		.compatible = "renesas,r8a7743-cpg-mssr",
		.data = &r8a7743_cpg_mssr_info,
	},
	/* RZ/G1N is (almost) identical to RZ/G1M w.r.t. clocks. */
	{
		.compatible = "renesas,r8a7744-cpg-mssr",
		.data = &r8a7743_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7745
	{
		.compatible = "renesas,r8a7745-cpg-mssr",
		.data = &r8a7745_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77470
	{
		.compatible = "renesas,r8a77470-cpg-mssr",
		.data = &r8a77470_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A774A1
	{
		.compatible = "renesas,r8a774a1-cpg-mssr",
		.data = &r8a774a1_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A774B1
	{
		.compatible = "renesas,r8a774b1-cpg-mssr",
		.data = &r8a774b1_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A774C0
	{
		.compatible = "renesas,r8a774c0-cpg-mssr",
		.data = &r8a774c0_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A774E1
	{
		.compatible = "renesas,r8a774e1-cpg-mssr",
		.data = &r8a774e1_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7790
	{
		.compatible = "renesas,r8a7790-cpg-mssr",
		.data = &r8a7790_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7791
	{
		.compatible = "renesas,r8a7791-cpg-mssr",
		.data = &r8a7791_cpg_mssr_info,
	},
	/* R-Car M2-N is (almost) identical to R-Car M2-W w.r.t. clocks. */
	{
		.compatible = "renesas,r8a7793-cpg-mssr",
		.data = &r8a7791_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7792
	{
		.compatible = "renesas,r8a7792-cpg-mssr",
		.data = &r8a7792_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7794
	{
		.compatible = "renesas,r8a7794-cpg-mssr",
		.data = &r8a7794_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A7795
	{
		.compatible = "renesas,r8a7795-cpg-mssr",
		.data = &r8a7795_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77960
	{
		.compatible = "renesas,r8a7796-cpg-mssr",
		.data = &r8a7796_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77961
	{
		.compatible = "renesas,r8a77961-cpg-mssr",
		.data = &r8a7796_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77965
	{
		.compatible = "renesas,r8a77965-cpg-mssr",
		.data = &r8a77965_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77970
	{
		.compatible = "renesas,r8a77970-cpg-mssr",
		.data = &r8a77970_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77980
	{
		.compatible = "renesas,r8a77980-cpg-mssr",
		.data = &r8a77980_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77990
	{
		.compatible = "renesas,r8a77990-cpg-mssr",
		.data = &r8a77990_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A77995
	{
		.compatible = "renesas,r8a77995-cpg-mssr",
		.data = &r8a77995_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A779A0
	{
		.compatible = "renesas,r8a779a0-cpg-mssr",
		.data = &r8a779a0_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A779F0
	{
		.compatible = "renesas,r8a779f0-cpg-mssr",
		.data = &r8a779f0_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A779G0
	{
		.compatible = "renesas,r8a779g0-cpg-mssr",
		.data = &r8a779g0_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R8A779H0
	{
		.compatible = "renesas,r8a779h0-cpg-mssr",
		.data = &r8a779h0_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R9A09G077
	{
		.compatible = "renesas,r9a09g077-cpg-mssr",
		.data = &r9a09g077_cpg_mssr_info,
	},
#endif
#ifdef CONFIG_CLK_R9A09G087
	{
		.compatible = "renesas,r9a09g087-cpg-mssr",
		.data = &r9a09g077_cpg_mssr_info,
	},
#endif
	{ /* sentinel */ }
};

static void cpg_mssr_del_clk_provider(void *data)
{
	of_clk_del_provider(data);
}

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM_PSCI_FW)
static int cpg_mssr_suspend_noirq(struct device *dev)
{
	struct cpg_mssr_priv *priv = dev_get_drvdata(dev);
	unsigned int reg;

	/* This is the best we can do to check for the presence of PSCI */
	if (!psci_ops.cpu_suspend)
		return 0;

	/* Save module registers with bits under our control */
	for (reg = 0; reg < ARRAY_SIZE(priv->smstpcr_saved); reg++) {
		if (priv->smstpcr_saved[reg].mask)
			priv->smstpcr_saved[reg].val =
				priv->reg_layout == CLK_REG_LAYOUT_RZ_A ?
				readb(priv->pub.base0 + priv->control_regs[reg]) :
				readl(priv->pub.base0 + priv->control_regs[reg]);
	}

	/* Save core clocks */
	raw_notifier_call_chain(&priv->pub.notifiers, PM_EVENT_SUSPEND, NULL);

	return 0;
}

static int cpg_mssr_resume_noirq(struct device *dev)
{
	struct cpg_mssr_priv *priv = dev_get_drvdata(dev);
	unsigned int reg;
	u32 mask, oldval, newval;
	int error;

	/* This is the best we can do to check for the presence of PSCI */
	if (!psci_ops.cpu_suspend)
		return 0;

	/* Restore core clocks */
	raw_notifier_call_chain(&priv->pub.notifiers, PM_EVENT_RESUME, NULL);

	/* Restore module clocks */
	for (reg = 0; reg < ARRAY_SIZE(priv->smstpcr_saved); reg++) {
		mask = priv->smstpcr_saved[reg].mask;
		if (!mask)
			continue;

		if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A)
			oldval = readb(priv->pub.base0 + priv->control_regs[reg]);
		else
			oldval = readl(priv->pub.base0 + priv->control_regs[reg]);
		newval = oldval & ~mask;
		newval |= priv->smstpcr_saved[reg].val & mask;
		if (newval == oldval)
			continue;

		if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A) {
			writeb(newval, priv->pub.base0 + priv->control_regs[reg]);
			/* dummy read to ensure write has completed */
			readb(priv->pub.base0 + priv->control_regs[reg]);
			barrier_data(priv->pub.base0 + priv->control_regs[reg]);
			continue;
		} else
			writel(newval, priv->pub.base0 + priv->control_regs[reg]);

		/* Wait until enabled clocks are really enabled */
		mask &= ~priv->smstpcr_saved[reg].val;
		if (!mask)
			continue;

		error = readl_poll_timeout_atomic(priv->pub.base0 + priv->status_regs[reg],
						oldval, !(oldval & mask), 0, 10);
		if (error)
			dev_warn(dev, "Failed to enable SMSTP%u[0x%x]\n", reg,
				 oldval & mask);
	}

	return 0;
}

static const struct dev_pm_ops cpg_mssr_pm = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(cpg_mssr_suspend_noirq,
				      cpg_mssr_resume_noirq)
};
#define DEV_PM_OPS	&cpg_mssr_pm
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP && CONFIG_ARM_PSCI_FW */

static void __init cpg_mssr_reserved_exit(struct cpg_mssr_priv *priv)
{
	kfree(priv->reserved_ids);
}

static int __init cpg_mssr_reserved_init(struct cpg_mssr_priv *priv,
					 const struct cpg_mssr_info *info)
{
	struct device_node *soc __free(device_node) = of_find_node_by_path("/soc");
	struct device_node *node;
	uint32_t args[MAX_PHANDLE_ARGS];
	unsigned int *ids = NULL;
	unsigned int num = 0;

	/*
	 * Because clk_disable_unused() will disable all unused clocks, the device which is assigned
	 * to a non-Linux system will be disabled when Linux is booted.
	 *
	 * To avoid such situation, renesas-cpg-mssr assumes the device which has
	 * status = "reserved" is assigned to a non-Linux system, and adds CLK_IGNORE_UNUSED flag
	 * to its CPG_MOD clocks.
	 * see also
	 *	cpg_mssr_register_mod_clk()
	 *
	 *	scif5: serial@e6f30000 {
	 *		...
	 * =>		clocks = <&cpg CPG_MOD 202>,
	 *			 <&cpg CPG_CORE R8A7795_CLK_S3D1>,
	 *			 <&scif_clk>;
	 *			 ...
	 *		 status = "reserved";
	 *	};
	 */
	for_each_reserved_child_of_node(soc, node) {
		struct of_phandle_iterator it;
		int rc;

		of_for_each_phandle(&it, rc, node, "clocks", "#clock-cells", -1) {
			int idx;

			if (it.node != priv->np)
				continue;

			if (of_phandle_iterator_args(&it, args, MAX_PHANDLE_ARGS) != 2)
				continue;

			if (args[0] != CPG_MOD)
				continue;

			ids = krealloc_array(ids, (num + 1), sizeof(*ids), GFP_KERNEL);
			if (!ids) {
				of_node_put(it.node);
				return -ENOMEM;
			}

			if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A)
				idx = MOD_CLK_PACK_10(args[1]);	/* for DEF_MOD_STB() */
			else
				idx = MOD_CLK_PACK(args[1]);	/* for DEF_MOD() */

			ids[num] = info->num_total_core_clks + idx;

			num++;
		}
	}

	priv->num_reserved_ids	= num;
	priv->reserved_ids	= ids;

	return 0;
}

static int __init cpg_mssr_common_init(struct device *dev,
				       struct device_node *np,
				       const struct cpg_mssr_info *info)
{
	struct cpg_mssr_priv *priv;
	unsigned int nclks, i;
	int error;

	if (info->init) {
		error = info->init(dev);
		if (error)
			return error;
	}

	nclks = info->num_total_core_clks + info->num_hw_mod_clks;
	priv = kzalloc(struct_size(priv, clks, nclks), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pub.clks = priv->clks;
	priv->np = np;
	priv->dev = dev;
	spin_lock_init(&priv->pub.rmw_lock);

	priv->pub.base0 = of_iomap(np, 0);
	if (!priv->pub.base0) {
		error = -ENOMEM;
		goto out_err;
	}
	if (info->reg_layout == CLK_REG_LAYOUT_RZ_T2H) {
		priv->pub.base1 = of_iomap(np, 1);
		if (!priv->pub.base1) {
			error = -ENOMEM;
			goto out_err;
		}
	}

	priv->num_core_clks = info->num_total_core_clks;
	priv->num_mod_clks = info->num_hw_mod_clks;
	priv->last_dt_core_clk = info->last_dt_core_clk;
	RAW_INIT_NOTIFIER_HEAD(&priv->pub.notifiers);
	priv->reg_layout = info->reg_layout;
	if (priv->reg_layout == CLK_REG_LAYOUT_RCAR_GEN2_AND_GEN3) {
		priv->status_regs = mstpsr;
		priv->control_regs = smstpcr;
		priv->reset_regs = srcr;
		priv->reset_clear_regs = srstclr;
	} else if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A) {
		priv->control_regs = stbcr;
	} else if (priv->reg_layout == CLK_REG_LAYOUT_RZ_T2H) {
		priv->control_regs = mstpcr_for_rzt2h;
	} else if (priv->reg_layout == CLK_REG_LAYOUT_RCAR_GEN4) {
		priv->status_regs = mstpsr_for_gen4;
		priv->control_regs = mstpcr_for_gen4;
		priv->reset_regs = srcr_for_gen4;
		priv->reset_clear_regs = srstclr_for_gen4;
	} else {
		error = -EINVAL;
		goto out_err;
	}

	for (i = 0; i < nclks; i++)
		priv->pub.clks[i] = ERR_PTR(-ENOENT);

	error = cpg_mssr_reserved_init(priv, info);
	if (error)
		goto out_err;

	error = of_clk_add_provider(np, cpg_mssr_clk_src_twocell_get, priv);
	if (error)
		goto reserve_err;

	cpg_mssr_priv = priv;

	return 0;

reserve_err:
	cpg_mssr_reserved_exit(priv);
out_err:
	if (priv->pub.base0)
		iounmap(priv->pub.base0);
	if (priv->pub.base1)
		iounmap(priv->pub.base1);
	kfree(priv);

	return error;
}

void __init cpg_mssr_early_init(struct device_node *np,
				const struct cpg_mssr_info *info)
{
	int error;
	int i;

	error = cpg_mssr_common_init(NULL, np, info);
	if (error)
		return;

	for (i = 0; i < info->num_early_core_clks; i++)
		cpg_mssr_register_core_clk(&info->early_core_clks[i], info,
					   cpg_mssr_priv);

	for (i = 0; i < info->num_early_mod_clks; i++)
		cpg_mssr_register_mod_clk(&info->early_mod_clks[i], info,
					  cpg_mssr_priv);

}

static int __init cpg_mssr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct cpg_mssr_info *info;
	struct cpg_mssr_priv *priv;
	unsigned int i;
	int error;

	info = of_device_get_match_data(dev);

	if (!cpg_mssr_priv) {
		error = cpg_mssr_common_init(dev, dev->of_node, info);
		if (error)
			return error;
	}

	priv = cpg_mssr_priv;
	priv->dev = dev;
	dev_set_drvdata(dev, priv);

	for (i = 0; i < info->num_core_clks; i++)
		cpg_mssr_register_core_clk(&info->core_clks[i], info, priv);

	for (i = 0; i < info->num_mod_clks; i++)
		cpg_mssr_register_mod_clk(&info->mod_clks[i], info, priv);

	error = devm_add_action_or_reset(dev,
					 cpg_mssr_del_clk_provider,
					 np);
	if (error)
		goto reserve_exit;

	error = cpg_mssr_add_clk_domain(dev, info->core_pm_clks,
					info->num_core_pm_clks);
	if (error)
		goto reserve_exit;

	/* Reset Controller not supported for Standby Control SoCs */
	if (priv->reg_layout == CLK_REG_LAYOUT_RZ_A ||
	    priv->reg_layout == CLK_REG_LAYOUT_RZ_T2H)
		goto reserve_exit;

	error = cpg_mssr_reset_controller_register(priv);

reserve_exit:
	cpg_mssr_reserved_exit(priv);

	return error;
}

static struct platform_driver cpg_mssr_driver = {
	.driver		= {
		.name	= "renesas-cpg-mssr",
		.of_match_table = cpg_mssr_match,
		.pm = DEV_PM_OPS,
	},
};

static int __init cpg_mssr_init(void)
{
	return platform_driver_probe(&cpg_mssr_driver, cpg_mssr_probe);
}

subsys_initcall(cpg_mssr_init);

void __init mssr_mod_nullify(struct mssr_mod_clk *mod_clks,
			     unsigned int num_mod_clks,
			     const unsigned int *clks, unsigned int n)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < num_mod_clks && j < n; i++)
		if (mod_clks[i].id == clks[j]) {
			mod_clks[i].name = NULL;
			j++;
		}
}

MODULE_DESCRIPTION("Renesas CPG/MSSR Driver");
