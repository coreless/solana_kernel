/*
 * linux/arch/arm/mach-omap2/id.c
 *
 * OMAP2 CPU identification code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/socinfo.h>
#include <linux/seq_file.h>

#include <asm/cputype.h>

#include <plat/common.h>
#include <plat/control.h>
#include <plat/cpu.h>
#ifdef CONFIG_ARM_OF
#include <mach/dt_path.h>
#include <asm/prom.h>
#endif

static struct omap_chip_id omap_chip;

static unsigned int omap_revision, omap_revision_id;
static char *rev_name = "ES1.0                         ";
static bool is_trimmed_omap4;
u32 omap3_features;

unsigned int omap_rev(void)
{
	return omap_revision;
}
EXPORT_SYMBOL(omap_rev);

/* Check OMAP3630 variants for SW tiering.
 * OMAP3630-0800:  Max ARM/DSP@800/660Mhz, OPP1G N-Target not fused.
 * OMAP3630-1000: Max ARM/DSP@1G/800Mhz, OPP1G N-Target fused,OPP1.3G not fused.
 * OMAP3630-1200: Max ARM/DSP@1.2G/65Mhz, OPP1.2G fused.
 * Should be called as early as possible, but after devtree is initialized.
 */
void get_omap3630_revision_id(void){

#ifdef CONFIG_ARM_OF
	struct device_node *omaprev_node;
	const void *omaprev_prop;

	omaprev_node = of_find_node_by_path(DT_PATH_CHOSEN);

	if (omaprev_node != NULL) {
		omaprev_prop = of_get_property(omaprev_node, \
			DT_PROP_CHOSEN_OMAP3630REV, NULL);
		if (omaprev_prop) {
			omap_revision_id = *(unsigned int *)omaprev_prop;
			printk(KERN_INFO"%s: Devtree omap3630-variant 0x%08x\n",
				 __func__, omap_revision_id);
		} else {
			omap_revision_id = OMAP_3630_1000;
			printk(KERN_INFO"%s: Default omap3630-variant 0x%08x\n",
				 __func__, omap_revision_id);
			}

		of_node_put(omaprev_node);
	} else
		printk(KERN_ERR"%s can not find Chosen@0 node\n", __func__);
#endif

}

unsigned int omap_rev_id(void)
{
	return omap_revision_id;
}
EXPORT_SYMBOL(omap_rev_id);

/**
 * omap_chip_is - test whether currently running OMAP matches a chip type
 * @oc: omap_chip_t to test against
 *
 * Test whether the currently-running OMAP chip matches the supplied
 * chip type 'oc'.  Returns 1 upon a match; 0 upon failure.
 */
int omap_chip_is(struct omap_chip_id oci)
{
	return (oci.oc & omap_chip.oc) ? 1 : 0;
}
EXPORT_SYMBOL(omap_chip_is);

int omap_type(void)
{
	u32 val = 0;

	if (cpu_is_omap24xx()) {
		val = omap_ctrl_readl(OMAP24XX_CONTROL_STATUS);
	} else if (cpu_is_omap34xx()) {
		val = omap_ctrl_readl(OMAP343X_CONTROL_STATUS);
	} else if (cpu_is_omap44xx()) {
		val = omap_ctrl_readl(OMAP4_CTRL_MODULE_CORE_STATUS);
	} else {
		pr_err("Cannot detect omap type!\n");
		goto out;
	}

	val &= OMAP2_DEVICETYPE_MASK;
	val >>= 8;

out:
	return val;
}
EXPORT_SYMBOL(omap_type);


/*----------------------------------------------------------------------------*/

#define OMAP_TAP_IDCODE		0x0204
#define OMAP_TAP_PROD_ID_0      0x0208
#define OMAP_TAP_PROD_ID_1      0x020c
#define OMAP_TAP_PROD_ID_2      0x0210
#define OMAP_TAP_PROD_ID_3      0x0214
#define OMAP_TAP_DIE_ID_0	0x0218
#define OMAP_TAP_DIE_ID_1	0x021C
#define OMAP_TAP_DIE_ID_2	0x0220
#define OMAP_TAP_DIE_ID_3	0x0224

#define read_tap_reg(reg)	__raw_readl(tap_base  + (reg))

struct omap_id {
	u16	hawkeye;	/* Silicon type (Hawkeye id) */
	u8	dev;		/* Device type from production_id reg */
	u32	type;		/* Combined type id copied to omap_revision */
};

/* Register values to detect the OMAP version */
static struct omap_id omap_ids[] __initdata = {
	{ .hawkeye = 0xb5d9, .dev = 0x0, .type = 0x24200024 },
	{ .hawkeye = 0xb5d9, .dev = 0x1, .type = 0x24201024 },
	{ .hawkeye = 0xb5d9, .dev = 0x2, .type = 0x24202024 },
	{ .hawkeye = 0xb5d9, .dev = 0x4, .type = 0x24220024 },
	{ .hawkeye = 0xb5d9, .dev = 0x8, .type = 0x24230024 },
	{ .hawkeye = 0xb68a, .dev = 0x0, .type = 0x24300024 },
};

static void __iomem *tap_base;
static u16 tap_prod_id;
#define SOCINFO_SZ             128
static char socinfo[SOCINFO_SZ];

void __init omap24xx_check_revision(void)
{
	int i, j, sz;
	u32 idcode, prod_id;
	u16 hawkeye;
	u8  dev_type, rev;

	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	prod_id = read_tap_reg(tap_prod_id);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0x0f;
	dev_type = (prod_id >> 16) & 0x0f;

	pr_debug("OMAP_TAP_IDCODE 0x%08x REV %i HAWKEYE 0x%04x MANF %03x\n",
		 idcode, rev, hawkeye, (idcode >> 1) & 0x7ff);
	pr_debug("OMAP_TAP_DIE_ID_0: 0x%08x\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_0));
	pr_debug("OMAP_TAP_DIE_ID_1: 0x%08x DEV_REV: %i\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_1),
		 (read_tap_reg(OMAP_TAP_DIE_ID_1) >> 28) & 0xf);
	pr_debug("OMAP_TAP_DIE_ID_2: 0x%08x\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_2));
	pr_debug("OMAP_TAP_DIE_ID_3: 0x%08x\n",
		 read_tap_reg(OMAP_TAP_DIE_ID_3));
	pr_debug("OMAP_TAP_PROD_ID_0: 0x%08x DEV_TYPE: %i\n",
		 prod_id, dev_type);

	/* Check hawkeye ids */
	for (i = 0; i < ARRAY_SIZE(omap_ids); i++) {
		if (hawkeye == omap_ids[i].hawkeye)
			break;
	}

	if (i == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP CPU id\n");
		return;
	}

	for (j = i; j < ARRAY_SIZE(omap_ids); j++) {
		if (dev_type == omap_ids[j].dev)
			break;
	}

	if (j == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP device type. "
				"Handling it as OMAP%04x\n",
				omap_ids[i].type >> 16);
		j = i;
	}

	sz = snprintf(socinfo, SOCINFO_SZ, "OMAP%04x", omap_rev() >> 16);
	if ((omap_rev() >> 8) & 0x0f)
		snprintf(socinfo + sz, SOCINFO_SZ - sz, "ES%x",
						(omap_rev() >> 12) & 0xf);
	pr_info("%s\n", socinfo);
}

#define OMAP3_CHECK_FEATURE(status,feat)				\
	if (((status & OMAP3_ ##feat## _MASK) 				\
		>> OMAP3_ ##feat## _SHIFT) != FEAT_ ##feat## _NONE) { 	\
		omap3_features |= OMAP3_HAS_ ##feat;			\
	}

void __init omap3_check_features(void)
{
	u32 status;

	omap3_features = 0;

	status = omap_ctrl_readl(OMAP3_CONTROL_OMAP_STATUS);

	OMAP3_CHECK_FEATURE(status, L2CACHE);
	OMAP3_CHECK_FEATURE(status, IVA);
	OMAP3_CHECK_FEATURE(status, SGX);
	OMAP3_CHECK_FEATURE(status, NEON);
	OMAP3_CHECK_FEATURE(status, ISP);
	if (cpu_is_omap3630())
		omap3_features |= OMAP3_HAS_192MHZ_CLK;

	/*
	 * TODO: Get additional info (where applicable)
	 *       e.g. Size of L2 cache.
	 */
}

void __init omap3_check_revision(void)
{
	u32 cpuid, idcode;
	u16 hawkeye;
	u8 rev;

	omap_chip.oc = CHIP_IS_OMAP3430;

	/*
	 * We cannot access revision registers on ES1.0.
	 * If the processor type is Cortex-A8 and the revision is 0x0
	 * it means its Cortex r0p0 which is 3430 ES1.0.
	 */
	cpuid = read_cpuid(CPUID_ID);
	if ((((cpuid >> 4) & 0xfff) == 0xc08) && ((cpuid & 0xf) == 0x0)) {
		omap_revision = OMAP3430_REV_ES1_0;
		omap_chip.oc |= CHIP_IS_OMAP3430ES1;
		return;
	}

	/*
	 * Detection for 34xx ES2.0 and above can be done with just
	 * hawkeye and rev. See TRM 1.5.2 Device Identification.
	 * Note that rev does not map directly to our defined processor
	 * revision numbers as ES1.0 uses value 0.
	 */
	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0xff;

	switch (hawkeye) {
	case 0xb7ae:
		/* Handle 34xx/35xx devices */
		switch (rev) {
		case 0: /* Take care of early samples */
		case 1:
			omap_revision = OMAP3430_REV_ES2_0;
			omap_chip.oc |= CHIP_IS_OMAP3430ES2;
			break;
		case 2:
			omap_revision = OMAP3430_REV_ES2_1;
			omap_chip.oc |= CHIP_IS_OMAP3430ES2;
			break;
		case 3:
			omap_revision = OMAP3430_REV_ES3_0;
			omap_chip.oc |= CHIP_IS_OMAP3430ES3_0;
			break;
		case 4:
			omap_revision = OMAP3430_REV_ES3_1;
			omap_chip.oc |= CHIP_IS_OMAP3430ES3_1;
			break;
		case 7:
		/* FALLTHROUGH */
		default:
			/* Use the latest known revision as default */
			omap_revision = OMAP3430_REV_ES3_1_2;

			/* REVISIT: Add CHIP_IS_OMAP3430ES3_1_2? */
			omap_chip.oc |= CHIP_IS_OMAP3430ES3_1;
		}
		break;
	case 0xb868:
		/* Handle OMAP35xx/AM35xx devices
		 *
		 * Set the device to be OMAP3505 here. Actual device
		 * is identified later based on the features.
		 *
		 * REVISIT: AM3505/AM3517 should have their own CHIP_IS
		 */
		omap_revision = OMAP3505_REV(rev);
		omap_chip.oc |= CHIP_IS_OMAP3430ES3_1;
		break;
	case 0xb891:
		/* Handle 36xx devices */
		switch (rev) {
		case 0: /* Take care of early samples */
			omap_revision = OMAP3630_REV_ES1_0;
			omap_chip.oc |= CHIP_IS_OMAP3630ES1;
			break;
		case 1:
			omap_revision = OMAP3630_REV_ES1_1;
			omap_chip.oc |= CHIP_IS_OMAP3630ES1_1;
			break;
		case 2:
		/* FALLTHROUGH */
		default:
			/* Unknown default to latest silicon rev as default*/
			omap_revision = OMAP3630_REV_ES1_2;
			omap_chip.oc |= CHIP_IS_OMAP3630ES1_2;
		}
	}
}

#define MAX_ID_STRING		(4*8 + 4)
#define DIE_ID_REG_BASE		(L4_44XX_PHYS + 0x2000)
#define DIE_ID_REG_OFFSET	0x200

static int phone_id_panic_report_omap4(struct notifier_block *this,
					unsigned long event, void *ptr)
{
	u32 reg;
	reg = DIE_ID_REG_BASE + DIE_ID_REG_OFFSET;
	pr_emerg("Die id:%08x%08x", omap_readl(reg + 0x10), \
				omap_readl(reg + 0xC));
	return NOTIFY_DONE;
}
static struct notifier_block phone_id_panic_notifier_omap4 = {
	.notifier_call	= phone_id_panic_report_omap4,
	.next		= NULL,
	.priority	= INT_MAX,
};

void __init omap4_check_revision(void)
{
	u32 idcode;
	u8 rev;
	u8 dot;
	u8 *type;
	u32 id[4] = { 0 };
	u32 prod_id[2] = { 0 };
	u32 reg;
	u32 omap_rev_reg;
	u8 id_string[MAX_ID_STRING];
	int sz, pos;
	u32 mpu_sldo, core_sldo, iva_sldo;

#if 0
	u16 hawkeye;
	/*
	 * The IC rev detection is done with hawkeye and rev.
	 * Note that rev does not map directly to defined processor
	 * revision numbers as ES1.0 uses value 0.
	 */
	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0xff;

	/*FIXME:  Check ES2 hawkeye and rev fields */
	switch (hawkeye) {
	case 0xb852:
		switch(rev) {
		case 0:
			omap_revision = OMAP4430_REV_ES1_0;
			omap_chip.oc |= CHIP_IS_OMAP4430ES1;
			rev = 1;
			break;
		case 1:
			omap_revision = OMAP4430_REV_ES2_0;
			omap_chip.oc |= CHIP_IS_OMAP4430ES2;
			rev = 2;
			break;
		default:
			omap_revision = OMAP4430_REV_ES2_0;
			omap_chip.oc |= CHIP_IS_OMAP4430ES2;
			rev = 2;
	}
	break;
	default:
		/* Unknown default to latest silicon rev as default*/
		omap_revision = OMAP4430_REV_ES2_0;
		omap_chip.oc |= CHIP_IS_OMAP4430ES2;
		rev = 2;
	}
#endif
	/*
	 * FIXME: Temporary ID check hack.
	 * Use ARM register to check the ES type
	 */
	idcode = read_cpuid(CPUID_ID);
	if (((idcode >> 4) & 0xfff) == 0xc09) {
		idcode &= 0xf;
		switch (idcode) {
		case 1:
			omap_revision = OMAP4430_REV_ES1_0;
			omap_chip.oc |= CHIP_IS_OMAP4430ES1;
			rev = 1;
			dot = 0;
			break;
		case 2:
			omap_rev_reg = (read_tap_reg(OMAP_TAP_IDCODE) >> 28);
			if (omap_rev_reg == 0x3) {
				omap_revision = OMAP4430_REV_ES2_1;
				omap_chip.oc |= CHIP_IS_OMAP4430ES2_1;
				rev = 2;
				dot = 1;
			} else if (omap_rev_reg >= 0x4) {
				omap_revision = OMAP4430_REV_ES2_2;
				omap_chip.oc |= CHIP_IS_OMAP4430ES2_2;
				rev = 2;
				dot = 2;
			} else {
				omap_revision = OMAP4430_REV_ES2_0;
				omap_chip.oc |= CHIP_IS_OMAP4430ES2;
				rev = 2;
				dot = 0;
			}
			break;
		default:
			omap_revision = OMAP4430_REV_ES2_2;
			omap_chip.oc |= CHIP_IS_OMAP4430ES2_2;
			rev = 2;
			dot = 2;
		}
	} else {
		/* Assume the latest version */
		omap_revision = OMAP4430_REV_ES2_2;
		omap_chip.oc |= CHIP_IS_OMAP4430ES2_2;
		rev = 2;
		dot = 2;
	}

	switch (omap_type()) {
	case OMAP2_DEVICE_TYPE_GP:
		type = "GP";
		break;
	case OMAP2_DEVICE_TYPE_EMU:
		type = "EMU";
		break;
	case OMAP2_DEVICE_TYPE_SEC:
		type = "HS";
	break;
	default:
		type = "bad-type";
		break;
	}

	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	pr_info("***********************");
	pr_info("OMAP%04x ES%d.%d type(%s)\n",
			omap_rev() >> 16, rev, dot, type);
	pr_info("id-code  (%x)\n", idcode);

	reg = DIE_ID_REG_BASE + DIE_ID_REG_OFFSET;
	/* Get Die-id */
	id[0] = omap_readl(reg);
	id[1] = omap_readl(reg + 0x8);
	id[2] = omap_readl(reg + 0xC);
	id[3] = omap_readl(reg + 0x10);
	/* die-id string */
	snprintf(id_string, MAX_ID_STRING, "%08X-%08X-%08X-%08X",
						id[3], id[2],
						id[1], id[0]);
	pr_info("Die-id   (%s)\n", id_string);

	/* Get prod-id */
	prod_id[0] = omap_readl(reg + 0x14);
	prod_id[1] = omap_readl(reg + 0x18);
	snprintf(id_string, MAX_ID_STRING, "%08X-%08X",
						prod_id[1], prod_id[0]);
	pr_info("Prod-id  (%s)\n", id_string);

	/* work around the untrimmed SLDO device */
	mpu_sldo = omap_readl(0x4a002324);
	iva_sldo = omap_readl(0x4a002320);
	core_sldo = omap_readl(0x4a002328);
	if (!mpu_sldo && !iva_sldo && !core_sldo) {
		is_trimmed_omap4 = false;
		pr_info("The device is NOT trimmed. Workaround:\n");
		omap_writel(0x401040f, 0x4a002320);
		omap_writel(0x401040f, 0x4a002324);
		omap_writel(0x401040f, 0x4a002328);
	} else {
		is_trimmed_omap4 = true;
		pr_info("The device is trimmed\n");
	}

	pr_info("SLDO: MPU=0x%x CORE=0x%x IVA=0x%x\n",
			omap_readl(0x4a002324), omap_readl(0x4a002328),
			omap_readl(0x4a002320));
	pr_info("***********************");

	/* Print socinfo information */
	sz = snprintf(socinfo, SOCINFO_SZ, "OMAP%04x ES%d.%d type(%s)",
			omap_rev() >> 16, rev, dot, type);
	pos = sz;
	sz = snprintf(socinfo + pos, SOCINFO_SZ - pos,
			"\nIDCODE\t: %08x", idcode);
	pos += sz;
	sz = snprintf(socinfo + pos, SOCINFO_SZ - pos,
			"\nPr. ID\t: %08x %08x", prod_id[1], prod_id[0]);
	pos += sz;
	snprintf(socinfo + pos, SOCINFO_SZ - pos,
		"\nDie ID\t: %08x %08x %08x %08x",
		id[3], id[2], id[1], id[0]);
}

/*
 * Some of the OMAP4430 ES2.2 units are not fully trimmed.
 * Trimming of SLDO, VBB, BGAP, VDAC & HDMI is missing.
 * This causes instability of the platform, especially at low OPPs.
 * These devices should not be used for system stabilization,
 * robustness, compliance testing.
 * Bits[9:5] of register CONTROL_LDOSRAM_MPU_VOLTAGE_CTRL are the
 * trimmed values.
 * When read, these bits are 00000 if the devices has not been
 * properly trimmed.
 */
void check_omap4430_trim_chip(void)
{
	if (!cpu_is_omap44xx())
		return;

	if (is_trimmed_omap4)
		pr_err("OMAP4430: trimmed, good phone!\n");
	else
		pr_err("OMAP4430: non-trimmed phone found!!! which "
			"is unexpected for system stabilization, robustness, "
			"compliance testing although software workaround "
			"has already been included.\n");
}

#define OMAP3_SHOW_FEATURE(feat)		\
	if (omap3_has_ ##feat())		\
		printk(#feat" ");

void __init omap3_cpuinfo(void)
{
	int sz;
	u8 rev = GET_OMAP_REVISION();
	char cpu_name[16], cpu_rev[16];

	/* OMAP3430 and OMAP3530 are assumed to be same.
	 *
	 * OMAP3525, OMAP3515 and OMAP3503 can be detected only based
	 * on available features. Upon detection, update the CPU id
	 * and CPU class bits.
	 */
	if (cpu_is_omap3630()) {
		strcpy(cpu_name, "OMAP3630");
	} else if (cpu_is_omap3505()) {
		/*
		 * AM35xx devices
		 */
		if (omap3_has_sgx()) {
			omap_revision = OMAP3517_REV(rev);
			strcpy(cpu_name, "AM3517");
		} else {
			/* Already set in omap3_check_revision() */
			strcpy(cpu_name, "AM3505");
		}
	} else if (omap3_has_iva() && omap3_has_sgx()) {
		/* OMAP3430, OMAP3525, OMAP3515, OMAP3503 devices */
		strcpy(cpu_name, "OMAP3430/3530");
	} else if (omap3_has_iva()) {
		omap_revision = OMAP3525_REV(rev);
		strcpy(cpu_name, "OMAP3525");
	} else if (omap3_has_sgx()) {
		omap_revision = OMAP3515_REV(rev);
		strcpy(cpu_name, "OMAP3515");
	} else {
		omap_revision = OMAP3503_REV(rev);
		strcpy(cpu_name, "OMAP3503");
	}

	switch (rev) {
	case OMAP_REVBITS_00:
		strcpy(cpu_rev, "1.0");
		break;
	case OMAP_REVBITS_01:
		strcpy(cpu_rev, "1.1");
		break;
	 case OMAP_REVBITS_02:
		strcpy(cpu_rev, "1.2");
		break;
	case OMAP_REVBITS_10:
		strcpy(cpu_rev, "2.0");
		break;
	case OMAP_REVBITS_20:
		strcpy(cpu_rev, "2.1");
		break;
	case OMAP_REVBITS_30:
		strcpy(cpu_rev, "3.0");
		break;
	case OMAP_REVBITS_40:
	/* FALLTHROUGH */
	default:
		/* Use the latest known revision as default */
		strcpy(cpu_rev, "3.1");
	}

	/* Print verbose information */
	sz = snprintf(socinfo, SOCINFO_SZ, "%s ES%s", cpu_name, cpu_rev);
	pr_info("%s (", socinfo);

	OMAP3_SHOW_FEATURE(l2cache);
	OMAP3_SHOW_FEATURE(iva);
	OMAP3_SHOW_FEATURE(sgx);
	OMAP3_SHOW_FEATURE(neon);
	OMAP3_SHOW_FEATURE(isp);
	OMAP3_SHOW_FEATURE(192mhz_clk);

	printk(")\n");

	/* Append OMAP3 IDCODE and Production ID to /proc/socinfo */
	snprintf(socinfo + sz, SOCINFO_SZ - sz,
			"\nIDCODE\t: %08x\nPr. ID\t: %08x %08x %08x %08x",
			read_tap_reg(OMAP_TAP_IDCODE),
			read_tap_reg(OMAP_TAP_PROD_ID_0),
			read_tap_reg(OMAP_TAP_PROD_ID_1),
			read_tap_reg(OMAP_TAP_PROD_ID_2),
			read_tap_reg(OMAP_TAP_PROD_ID_3));

}

#ifdef CONFIG_OMAP3_EXPORT_DIE_ID
static int __init omap3_die_id_setup(char *s)
{
	int sz;

	sz = strlen(socinfo);
	snprintf(socinfo + sz, SOCINFO_SZ - sz,
			"\nDie ID\t: %08x %08x %08x %08x",
			read_tap_reg(OMAP_TAP_DIE_ID_0),
			read_tap_reg(OMAP_TAP_DIE_ID_1),
			read_tap_reg(OMAP_TAP_DIE_ID_2),
			read_tap_reg(OMAP_TAP_DIE_ID_3));

	return 1;
}
__setup("omap3_die_id", omap3_die_id_setup);
#endif

static int omap_socinfo_show(struct seq_file *m, void *v)
{
	char *socinfop = v;

	seq_printf(m, "SoC\t: %s\n", socinfop);

	return 0;
}

static int phone_id_panic_report(struct notifier_block *this,
					unsigned long event, void *ptr)
{
	pr_emerg("Phone id:%x%x", read_tap_reg(OMAP_TAP_DIE_ID_0), \
					read_tap_reg(OMAP_TAP_DIE_ID_1));
	return NOTIFY_DONE;
}
static struct notifier_block phone_id_panic_notifier = {
	.notifier_call	= phone_id_panic_report,
	.next		= NULL,
	.priority	= INT_MAX,
};

/*
 * Try to detect the exact revision of the omap we're running on
 */
void __init omap2_check_revision(void)
{
	/*
	 * At this point we have an idea about the processor revision set
	 * earlier with omap2_set_globals_tap().
	 */
	if (cpu_is_omap24xx()) {
		omap24xx_check_revision();
	} else if (cpu_is_omap34xx()) {
		omap3_check_revision();
		omap3_check_features();
		omap3_cpuinfo();
	} else if (cpu_is_omap44xx()) {
		omap4_check_revision();
	} else {
		pr_err("OMAP revision unknown, please fix!\n");
	}

	/* also register call back to report SoC data under /proc/socinfo */
	register_socinfo_show(omap_socinfo_show, socinfo);

	/*
	 * OK, now we know the exact revision. Initialize omap_chip bits
	 * for powerdowmain and clockdomain code.
	 */
	if (cpu_is_omap243x()) {
		/* Currently only supports 2430ES2.1 and 2430-all */
		omap_chip.oc |= CHIP_IS_OMAP2430;
	} else if (cpu_is_omap242x()) {
		/* Currently only supports 2420ES2.1.1 and 2420-all */
		omap_chip.oc |= CHIP_IS_OMAP2420;
	} else {
		pr_err("Uninitialized omap_chip, please fix!\n");
	}

	if (cpu_is_omap44xx()) {
		atomic_notifier_chain_register(&panic_notifier_list,
					&phone_id_panic_notifier_omap4);
	} else {
		atomic_notifier_chain_register(&panic_notifier_list,
					&phone_id_panic_notifier);
	}
}

/*
 * Set up things for map_io and processor detection later on. Gets called
 * pretty much first thing from board init. For multi-omap, this gets
 * cpu_is_omapxxxx() working accurately enough for map_io. Then we'll try to
 * detect the exact revision later on in omap2_detect_revision() once map_io
 * is done.
 */
void __init omap2_set_globals_tap(struct omap_globals *omap2_globals)
{
	omap_revision = omap2_globals->class;
	tap_base = omap2_globals->tap;

	if (cpu_is_omap34xx())
		tap_prod_id = 0x0210;
	else
		tap_prod_id = 0x0208;
}

/*
 * Get OMAP chip version details from bootargs
 */
int  omap34xx_get_omap_version(char *str)
{
	unsigned int rev_id;
	if (get_option(&str, &rev_id) == 1) {
		switch (rev_id) {
		case 3420:
			omap_revision_id = OMAP_3420;
			omap_revision = OMAP3430_REV_ES3_1;
			omap_chip.oc |= CHIP_IS_OMAP3430ES3_1;
			rev_name = "ES3.1";
			break;
		case 3430:
			omap_revision_id = OMAP_3430;
			omap_revision = OMAP3430_REV_ES3_1;
			omap_chip.oc |= CHIP_IS_OMAP3430ES3_1;
			rev_name = "ES3.1";
			break;
		case 3440:
			omap_revision_id = OMAP_3440;
			omap_revision = OMAP3430_REV_ES3_1_1;
			omap_chip.oc |= CHIP_IS_OMAP3430ES3_1_1;
			rev_name = "ES3.1.1";
			break;
		case 3630:
			omap_revision_id = OMAP_3630;
			omap_chip.oc |= CHIP_IS_OMAP3630ES1;
			omap_revision = OMAP3630_REV_ES1_0;
			break;
		case 36300800:
			omap_revision_id = OMAP_3630_0800;
			omap_chip.oc |= CHIP_IS_OMAP3630ES1;
			omap_revision = OMAP3630_REV_ES1_0;
			break;
		case 36301000:
			omap_revision_id = OMAP_3630_1000;
			omap_chip.oc |= CHIP_IS_OMAP3630ES1;
			omap_revision = OMAP3630_REV_ES1_0;
			break;
		default:
			pr_err("OMAP revision unknown, please fix!\n");
			return 1;
		}
		pr_info("OMAP%04x %s\n", omap_rev() >> 16, rev_name);
		pr_info("OMAP Version is OMAP%04d\n", rev_id);
	}

	return 1;
}

__setup("omap_version=", omap34xx_get_omap_version);

