// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Broadcom BCM2835 SoC temperature sensor
 *
 * Copyright (C) 2016 Martin Sperl
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "../thermal_hwmon.h"

#define BCM2835_TS_TSENSCTL			0x00
#define BCM2835_TS_TSENSSTAT			0x04

#define BCM2835_TS_TSENSCTL_PRWDW		BIT(0)
#define BCM2835_TS_TSENSCTL_RSTB		BIT(1)

/*
 * bandgap reference voltage in 6 mV increments
 * 000b = 1178 mV, 001b = 1184 mV, ... 111b = 1220 mV
 */
#define BCM2835_TS_TSENSCTL_CTRL_BITS		3
#define BCM2835_TS_TSENSCTL_CTRL_SHIFT		2
#define BCM2835_TS_TSENSCTL_CTRL_MASK		    \
	GENMASK(BCM2835_TS_TSENSCTL_CTRL_BITS +     \
		BCM2835_TS_TSENSCTL_CTRL_SHIFT - 1, \
		BCM2835_TS_TSENSCTL_CTRL_SHIFT)
#define BCM2835_TS_TSENSCTL_CTRL_DEFAULT	1
#define BCM2835_TS_TSENSCTL_EN_INT		BIT(5)
#define BCM2835_TS_TSENSCTL_DIRECT		BIT(6)
#define BCM2835_TS_TSENSCTL_CLR_INT		BIT(7)
#define BCM2835_TS_TSENSCTL_THOLD_SHIFT		8
#define BCM2835_TS_TSENSCTL_THOLD_BITS		10
#define BCM2835_TS_TSENSCTL_THOLD_MASK		     \
	GENMASK(BCM2835_TS_TSENSCTL_THOLD_BITS +     \
		BCM2835_TS_TSENSCTL_THOLD_SHIFT - 1, \
		BCM2835_TS_TSENSCTL_THOLD_SHIFT)
/*
 * time how long the block to be asserted in reset
 * which based on a clock counter (TSENS clock assumed)
 */
#define BCM2835_TS_TSENSCTL_RSTDELAY_SHIFT	18
#define BCM2835_TS_TSENSCTL_RSTDELAY_BITS	8
#define BCM2835_TS_TSENSCTL_REGULEN		BIT(26)

#define BCM2835_TS_TSENSSTAT_DATA_BITS		10
#define BCM2835_TS_TSENSSTAT_DATA_SHIFT		0
#define BCM2835_TS_TSENSSTAT_DATA_MASK		     \
	GENMASK(BCM2835_TS_TSENSSTAT_DATA_BITS +     \
		BCM2835_TS_TSENSSTAT_DATA_SHIFT - 1, \
		BCM2835_TS_TSENSSTAT_DATA_SHIFT)
#define BCM2835_TS_TSENSSTAT_VALID		BIT(10)
#define BCM2835_TS_TSENSSTAT_INTERRUPT		BIT(11)

struct bcm2835_thermal_data {
	struct thermal_zone_device *tz;
	void __iomem *regs;
	struct clk *clk;
	struct dentry *debugfsdir;
};

static int bcm2835_thermal_adc2temp(u32 adc, int offset, int slope)
{
	return offset + slope * adc;
}

static int bcm2835_thermal_temp2adc(int temp, int offset, int slope)
{
	temp -= offset;
	temp /= slope;

	if (temp < 0)
		temp = 0;
	if (temp >= BIT(BCM2835_TS_TSENSSTAT_DATA_BITS))
		temp = BIT(BCM2835_TS_TSENSSTAT_DATA_BITS) - 1;

	return temp;
}

static int bcm2835_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct bcm2835_thermal_data *data = thermal_zone_device_priv(tz);
	u32 val = readl(data->regs + BCM2835_TS_TSENSSTAT);

	if (!(val & BCM2835_TS_TSENSSTAT_VALID))
		return -EIO;

	val &= BCM2835_TS_TSENSSTAT_DATA_MASK;

	*temp = bcm2835_thermal_adc2temp(
		val,
		thermal_zone_get_offset(data->tz),
		thermal_zone_get_slope(data->tz));

	return 0;
}

static const struct debugfs_reg32 bcm2835_thermal_regs[] = {
	{
		.name = "ctl",
		.offset = 0
	},
	{
		.name = "stat",
		.offset = 4
	}
};

static void bcm2835_thermal_debugfs(struct platform_device *pdev)
{
	struct bcm2835_thermal_data *data = platform_get_drvdata(pdev);
	struct debugfs_regset32 *regset;

	data->debugfsdir = debugfs_create_dir("bcm2835_thermal", NULL);

	regset = devm_kzalloc(&pdev->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = bcm2835_thermal_regs;
	regset->nregs = ARRAY_SIZE(bcm2835_thermal_regs);
	regset->base = data->regs;

	debugfs_create_regset32("regset", 0444, data->debugfsdir, regset);
}

static const struct thermal_zone_device_ops bcm2835_thermal_ops = {
	.get_temp = bcm2835_thermal_get_temp,
};

/*
 * Note: as per Raspberry Foundation FAQ
 * (https://www.raspberrypi.org/help/faqs/#performanceOperatingTemperature)
 * the recommended temperature range for the SoC -40C to +85C
 * so the trip limit is set to 80C.
 * this applies to all the BCM283X SoC
 */

static const struct of_device_id bcm2835_thermal_of_match_table[] = {
	{
		.compatible = "brcm,bcm2835-thermal",
	},
	{
		.compatible = "brcm,bcm2836-thermal",
	},
	{
		.compatible = "brcm,bcm2837-thermal",
	},
	{},
};
MODULE_DEVICE_TABLE(of, bcm2835_thermal_of_match_table);

static int bcm2835_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct thermal_zone_device *tz;
	struct bcm2835_thermal_data *data;
	int err = 0;
	u32 val;
	unsigned long rate;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	match = of_match_device(bcm2835_thermal_of_match_table, dev);
	if (!match)
		return -EINVAL;

	data->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(data->regs)) {
		err = PTR_ERR(data->regs);
		return err;
	}

	data->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(data->clk))
		return dev_err_probe(dev, PTR_ERR(data->clk), "Could not get clk\n");

	rate = clk_get_rate(data->clk);
	if ((rate < 1920000) || (rate > 5000000))
		dev_warn(dev,
			 "Clock %pC running at %lu Hz is outside of the recommended range: 1.92 to 5MHz\n",
			 data->clk, rate);

	/* register of thermal sensor and get info from DT */
	tz = devm_thermal_of_zone_register(dev, 0, data, &bcm2835_thermal_ops);
	if (IS_ERR(tz))
		return dev_err_probe(dev, PTR_ERR(tz), "Failed to register the thermal device\n");

	/*
	 * right now the FW does set up the HW-block, so we are not
	 * touching the configuration registers.
	 * But if the HW is not enabled, then set it up
	 * using "sane" values used by the firmware right now.
	 */
	val = readl(data->regs + BCM2835_TS_TSENSCTL);
	if (!(val & BCM2835_TS_TSENSCTL_RSTB)) {
		int offset, slope, crit_temp;

		slope = thermal_zone_get_slope(tz);
		offset = thermal_zone_get_offset(tz);
		/*
		 * For now we deal only with critical, otherwise
		 * would need to iterate
		 */
		err = thermal_zone_get_crit_temp(tz, &crit_temp);
		if (err < 0) {
			dev_err(dev, "Not able to read trip_temp: %d\n", err);
			return err;
		}

		/* set bandgap reference voltage and enable voltage regulator */
		val = (BCM2835_TS_TSENSCTL_CTRL_DEFAULT <<
		       BCM2835_TS_TSENSCTL_CTRL_SHIFT) |
		      BCM2835_TS_TSENSCTL_REGULEN;

		/* use the recommended reset duration */
		val |= (0xFE << BCM2835_TS_TSENSCTL_RSTDELAY_SHIFT);

		/*  trip_adc value from info */
		val |= bcm2835_thermal_temp2adc(crit_temp,
						offset,
						slope)
			<< BCM2835_TS_TSENSCTL_THOLD_SHIFT;

		/* write the value back to the register as 2 steps */
		writel(val, data->regs + BCM2835_TS_TSENSCTL);
		val |= BCM2835_TS_TSENSCTL_RSTB;
		writel(val, data->regs + BCM2835_TS_TSENSCTL);
	}

	data->tz = tz;

	platform_set_drvdata(pdev, data);

	/*
	 * Thermal_zone doesn't enable hwmon as default,
	 * enable it here
	 */
	err = thermal_add_hwmon_sysfs(tz);
	if (err)
		return err;

	bcm2835_thermal_debugfs(pdev);

	return 0;
}

static void bcm2835_thermal_remove(struct platform_device *pdev)
{
	struct bcm2835_thermal_data *data = platform_get_drvdata(pdev);

	debugfs_remove_recursive(data->debugfsdir);
}

static struct platform_driver bcm2835_thermal_driver = {
	.probe = bcm2835_thermal_probe,
	.remove = bcm2835_thermal_remove,
	.driver = {
		.name = "bcm2835_thermal",
		.of_match_table = bcm2835_thermal_of_match_table,
	},
};
module_platform_driver(bcm2835_thermal_driver);

MODULE_AUTHOR("Martin Sperl");
MODULE_DESCRIPTION("Thermal driver for bcm2835 chip");
MODULE_LICENSE("GPL");
