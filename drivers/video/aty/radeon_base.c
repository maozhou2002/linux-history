/*
 *	drivers/video/radeonfb.c
 *	framebuffer driver for ATI Radeon chipset video boards
 *
 *	Copyright 2003	Ben. Herrenschmidt <benh@kernel.crashing.org>
 *	Copyright 2000	Ani Joshi <ajoshi@kernel.crashing.org>
 *
 *	i2c bits from Luca Tettamanti <kronos@kronoz.cjb.net>
 *	
 *	Special thanks to ATI DevRel team for their hardware donations.
 *
 *	...Insert GPL boilerplate here...
 *
 *	Significant portions of this driver apdated from XFree86 Radeon
 *	driver which has the following copyright notice:
 *
 *	Copyright 2000 ATI Technologies Inc., Markham, Ontario, and
 *                     VA Linux Systems Inc., Fremont, California.
 *
 *	All Rights Reserved.
 *
 *	Permission is hereby granted, free of charge, to any person obtaining
 *	a copy of this software and associated documentation files (the
 *	"Software"), to deal in the Software without restriction, including
 *	without limitation on the rights to use, copy, modify, merge,
 *	publish, distribute, sublicense, and/or sell copies of the Software,
 *	and to permit persons to whom the Software is furnished to do so,
 *	subject to the following conditions:
 *
 *	The above copyright notice and this permission notice (including the
 *	next paragraph) shall be included in all copies or substantial
 *	portions of the Software.
 *
 *	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * 	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *	NON-INFRINGEMENT.  IN NO EVENT SHALL ATI, VA LINUX SYSTEMS AND/OR
 *	THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *	WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *	DEALINGS IN THE SOFTWARE.
 *
 *	XFree86 driver authors:
 *
 *	   Kevin E. Martin <martin@xfree86.org>
 *	   Rickard E. Faith <faith@valinux.com>
 *	   Alan Hourihane <alanh@fairlite.demon.co.uk>
 *
 */


#define RADEON_VERSION	"0.2.0"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/i2c.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#ifdef CONFIG_PPC_OF

#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include "../macmodes.h"

#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

#ifdef CONFIG_BOOTX_TEXT
#include <asm/btext.h>
#endif

#endif /* CONFIG_PPC_OF */

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include <video/radeon.h>
#include <linux/radeonfb.h>

#include "../edid.h" // MOVE THAT TO include/video
#include "ati_ids.h"
#include "radeonfb.h"		    

#define MAX_MAPPED_VRAM	(2048*2048*4)
#define MIN_MAPPED_VRAM	(1024*768*1)

#define CHIP_DEF(id, family, flags)					\
	{ PCI_VENDOR_ID_ATI, id, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (flags) | (CHIP_FAMILY_##family) }

static struct pci_device_id radeonfb_pci_table[] = {
	/* Mobility M6 */
	CHIP_DEF(PCI_CHIP_RADEON_LY, 	RV100,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RADEON_LZ,	RV100,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	/* Radeon VE/7000 */
	CHIP_DEF(PCI_CHIP_RV100_QY, 	RV100,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV100_QZ, 	RV100,	CHIP_HAS_CRTC2),
	/* Radeon IGP320M (U1) */
	CHIP_DEF(PCI_CHIP_RS100_4336,	RS100,	CHIP_HAS_CRTC2 | CHIP_IS_IGP | CHIP_IS_MOBILITY),
	/* Radeon IGP320 (A3) */
	CHIP_DEF(PCI_CHIP_RS100_4136,	RS100,	CHIP_HAS_CRTC2 | CHIP_IS_IGP), 
	/* IGP330M/340M/350M (U2) */
	CHIP_DEF(PCI_CHIP_RS200_4337,	RS200,	CHIP_HAS_CRTC2 | CHIP_IS_IGP | CHIP_IS_MOBILITY),
	/* IGP330/340/350 (A4) */
	CHIP_DEF(PCI_CHIP_RS200_4137,	RS200,	CHIP_HAS_CRTC2 | CHIP_IS_IGP),
	/* Mobility 7000 IGP */
	CHIP_DEF(PCI_CHIP_RS250_4437,	RS200,	CHIP_HAS_CRTC2 | CHIP_IS_IGP | CHIP_IS_MOBILITY),
	/* 7000 IGP (A4+) */
	CHIP_DEF(PCI_CHIP_RS250_4237,	RS200,	CHIP_HAS_CRTC2 | CHIP_IS_IGP),
	/* 8500 AIW */
	CHIP_DEF(PCI_CHIP_R200_BB,	R200,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R200_BC,	R200,	CHIP_HAS_CRTC2),
	/* 8700/8800 */
	CHIP_DEF(PCI_CHIP_R200_QH,	R200,	CHIP_HAS_CRTC2),
	/* 8500 */
	CHIP_DEF(PCI_CHIP_R200_QL,	R200,	CHIP_HAS_CRTC2),
	/* 9100 */
	CHIP_DEF(PCI_CHIP_R200_QM,	R200,	CHIP_HAS_CRTC2),
	/* Mobility M7 */
	CHIP_DEF(PCI_CHIP_RADEON_LW,	RV200,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RADEON_LX,	RV200,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	/* 7500 */
	CHIP_DEF(PCI_CHIP_RV200_QW,	RV200,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV200_QX,	RV200,	CHIP_HAS_CRTC2),
	/* Mobility M9 */
	CHIP_DEF(PCI_CHIP_RV250_Ld,	RV250,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV250_Le,	RV250,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV250_Lf,	RV250,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV250_Lg,	RV250,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	/* 9000/Pro */
	CHIP_DEF(PCI_CHIP_RV250_If,	RV250,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV250_Ig,	RV250,	CHIP_HAS_CRTC2),
	/* Mobility 9100 IGP (U3) */
	CHIP_DEF(PCI_CHIP_RS300_5835,	RS300,	CHIP_HAS_CRTC2 | CHIP_IS_IGP | CHIP_IS_MOBILITY),
	/* 9100 IGP (A5) */
	CHIP_DEF(PCI_CHIP_RS300_5834,	RS300,	CHIP_HAS_CRTC2 | CHIP_IS_IGP),
	/* Mobility 9200 (M9+) */
	CHIP_DEF(PCI_CHIP_RV280_5C61,	RV280,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV280_5C63,	RV280,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	/* 9200 */
	CHIP_DEF(PCI_CHIP_RV280_5960,	RV280,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV280_5961,	RV280,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV280_5962,	RV280,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV280_5964,	RV280,	CHIP_HAS_CRTC2),
	/* 9500 */
	CHIP_DEF(PCI_CHIP_R300_AD,	R300,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R300_AE,	R300,	CHIP_HAS_CRTC2),
	/* 9600TX / FireGL Z1 */
	CHIP_DEF(PCI_CHIP_R300_AF,	R300,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R300_AG,	R300,	CHIP_HAS_CRTC2),
	/* 9700/9500/Pro/FireGL X1 */
	CHIP_DEF(PCI_CHIP_R300_ND,	R300,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R300_NE,	R300,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R300_NF,	R300,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R300_NG,	R300,	CHIP_HAS_CRTC2),
	/* Mobility M10/M11 */
	CHIP_DEF(PCI_CHIP_RV350_NP,	RV350,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV350_NQ,	RV350,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV350_NR,	RV350,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV350_NS,	RV350,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV350_NT,	RV350,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	CHIP_DEF(PCI_CHIP_RV350_NV,	RV350,	CHIP_HAS_CRTC2 | CHIP_IS_MOBILITY),
	/* 9600/FireGL T2 */
	CHIP_DEF(PCI_CHIP_RV350_AP,	RV350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV350_AQ,	RV350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV360_AR,	RV350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV350_AS,	RV350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV350_AT,	RV350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_RV350_AV,	RV350,	CHIP_HAS_CRTC2),
	/* 9800/Pro/FileGL X2 */
	CHIP_DEF(PCI_CHIP_R350_AH,	R350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R350_AI,	R350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R350_AJ,	R350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R350_AK,	R350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R350_NH,	R350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R350_NI,	R350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R360_NJ,	R350,	CHIP_HAS_CRTC2),
	CHIP_DEF(PCI_CHIP_R350_NK,	R350,	CHIP_HAS_CRTC2),
	/* Original Radeon/7200 */
	CHIP_DEF(PCI_CHIP_RADEON_QD,	RADEON,	0),
	CHIP_DEF(PCI_CHIP_RADEON_QE,	RADEON,	0),
	CHIP_DEF(PCI_CHIP_RADEON_QF,	RADEON,	0),
	CHIP_DEF(PCI_CHIP_RADEON_QG,	RADEON,	0),
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, radeonfb_pci_table);


typedef struct {
	u16 reg;
	u32 val;
} reg_val;


/* these common regs are cleared before mode setting so they do not
 * interfere with anything
 */
static reg_val common_regs[] = {
	{ OVR_CLR, 0 },	
	{ OVR_WID_LEFT_RIGHT, 0 },
	{ OVR_WID_TOP_BOTTOM, 0 },
	{ OV0_SCALE_CNTL, 0 },
	{ SUBPIC_CNTL, 0 },
	{ VIPH_CONTROL, 0 },
	{ I2C_CNTL_1, 0 },
	{ GEN_INT_CNTL, 0 },
	{ CAP0_TRIG_CNTL, 0 },
	{ CAP1_TRIG_CNTL, 0 },
};

/*
 * globals
 */
        
static char *mode_option;
static char *monitor_layout;
static int noaccel = 0;
static int nomodeset = 0;
static int ignore_edid = 0;
static int mirror = 0;
static int panel_yres = 0;
static int force_dfp = 0;
static int force_measure_pll = 0;
#ifdef CONFIG_MTRR
static int nomtrr = 0;
#endif

int radeonfb_noaccel = 0;

/*
 * prototypes
 */


#ifdef CONFIG_PPC_OF

#ifdef CONFIG_PMAC_BACKLIGHT
static int radeon_set_backlight_enable(int on, int level, void *data);
static int radeon_set_backlight_level(int level, void *data);
static struct backlight_controller radeon_backlight_controller = {
	radeon_set_backlight_enable,
	radeon_set_backlight_level
};
#endif /* CONFIG_PMAC_BACKLIGHT */

#endif /* CONFIG_PPC_OF */

static void __devexit radeon_unmap_ROM(struct radeonfb_info *rinfo, struct pci_dev *dev)
{
	// leave it disabled and unassigned
	struct resource *r = &dev->resource[PCI_ROM_RESOURCE];
	
	if (!rinfo->bios_seg)
		return;
	iounmap(rinfo->bios_seg);
	
	/* Release the ROM resource if we used it in the first place */
	if (r->parent && r->flags & PCI_ROM_ADDRESS_ENABLE) {
		release_resource(r);
		r->flags &= ~PCI_ROM_ADDRESS_ENABLE;
		r->end -= r->start;
		r->start = 0;
	}
	/* This will disable and set address to unassigned */
	pci_write_config_dword(dev, dev->rom_base_reg, 0);
}

static int __devinit radeon_map_ROM(struct radeonfb_info *rinfo, struct pci_dev *dev)
{
	void *rom;
	struct resource *r;
	u16 dptr;
	u8 rom_type;

	/* If this is a primary card, there is a shadow copy of the
	 * ROM somewhere in the first meg. We will just ignore the copy
	 * and use the ROM directly.
	 */
    
    	/* Fix from ATI for problem with Radeon hardware not leaving ROM enabled */
    	unsigned int temp;
	temp = INREG(MPP_TB_CONFIG);
	temp &= 0x00ffffffu;
	temp |= 0x04 << 24;
	OUTREG(MPP_TB_CONFIG, temp);
	temp = INREG(MPP_TB_CONFIG);
                                                                                                          
	/* no need to search for the ROM, just ask the card where it is. */
	r = &dev->resource[PCI_ROM_RESOURCE];
	
	/* assign the ROM an address if it doesn't have one */
	if (r->parent == NULL)
		pci_assign_resource(dev, PCI_ROM_RESOURCE);
	
	/* enable if needed */
	if (!(r->flags & PCI_ROM_ADDRESS_ENABLE)) {
		pci_write_config_dword(dev, dev->rom_base_reg,
				       r->start | PCI_ROM_ADDRESS_ENABLE);
		r->flags |= PCI_ROM_ADDRESS_ENABLE;
	}
	
	rom = ioremap(r->start, r->end - r->start + 1);
	if (!rom) {
		printk(KERN_ERR "radeonfb: ROM failed to map\n");
		return -ENOMEM;
	}
	
	rinfo->bios_seg = rom;

	/* Very simple test to make sure it appeared */
	if (BIOS_IN16(0) != 0xaa55) {
		printk(KERN_ERR "radeonfb: Invalid ROM signature %x should be 0xaa55\n",
		       BIOS_IN16(0));
		goto failed;
	}
	/* Look for the PCI data to check the ROM type */
	dptr = BIOS_IN16(0x18);

	/* Check the PCI data signature. If it's wrong, we still assume a normal x86 ROM
	 * for now, until I've verified this works everywhere. The goal here is more
	 * to phase out Open Firmware images.
	 *
	 * Currently, we only look at the first PCI data, we could iteratre and deal with
	 * them all, and we should use fb_bios_start relative to start of image and not
	 * relative start of ROM, but so far, I never found a dual-image ATI card
	 *
	 * typedef struct {
	 * 	u32	signature;	+ 0x00
	 * 	u16	vendor;		+ 0x04
	 * 	u16	device;		+ 0x06
	 * 	u16	reserved_1;	+ 0x08
	 * 	u16	dlen;		+ 0x0a
	 * 	u8	drevision;	+ 0x0c
	 * 	u8	class_hi;	+ 0x0d
	 * 	u16	class_lo;	+ 0x0e
	 * 	u16	ilen;		+ 0x10
	 * 	u16	irevision;	+ 0x12
	 * 	u8	type;		+ 0x14
	 * 	u8	indicator;	+ 0x15
	 * 	u16	reserved_2;	+ 0x16
	 * } pci_data_t;
	 */
	if (BIOS_IN32(dptr) !=  (('R' << 24) | ('I' << 16) | ('C' << 8) | 'P')) {
		printk(KERN_WARNING "radeonfb: PCI DATA signature in ROM incorrect: %08x\n",
		       BIOS_IN32(dptr));
		goto anyway;
	}
	rom_type = BIOS_IN8(dptr + 0x14);
	switch(rom_type) {
	case 0:
		printk(KERN_INFO "radeonfb: Found Intel x86 BIOS ROM Image\n");
		break;
	case 1:
		printk(KERN_INFO "radeonfb: Found Open Firmware ROM Image\n");
		goto failed;
	case 2:
		printk(KERN_INFO "radeonfb: Found HP PA-RISC ROM Image\n");
		goto failed;
	default:
		printk(KERN_INFO "radeonfb: Found unknown type %d ROM Image\n", rom_type);
		goto failed;
	}
 anyway:
	/* Locate the flat panel infos, do some sanity checking !!! */
	rinfo->fp_bios_start = BIOS_IN16(0x48);
	return 0;

 failed:
	rinfo->bios_seg = NULL;
	radeon_unmap_ROM(rinfo, dev);
	return -ENXIO;
}

#ifdef __i386__
static int  __devinit radeon_find_mem_vbios(struct radeonfb_info *rinfo)
{
	/* I simplified this code as we used to miss the signatures in
	 * a lot of case. It's now closer to XFree, we just don't check
	 * for signatures at all... Something better will have to be done
	 * if we end up having conflicts
	 */
        u32  segstart;
        unsigned char *rom_base = NULL;
                                                
        for(segstart=0x000c0000; segstart<0x000f0000; segstart+=0x00001000) {
                rom_base = (char *)ioremap(segstart, 0x10000);
		if (rom_base == NULL)
			return -ENOMEM;
                if ((*rom_base == 0x55) && (((*(rom_base + 1)) & 0xff) == 0xaa))
	                break;
                iounmap(rom_base);
		rom_base = NULL;
        }
	if (rom_base == NULL)
		return -ENXIO;

	/* Locate the flat panel infos, do some sanity checking !!! */
	rinfo->bios_seg = rom_base;
	rinfo->fp_bios_start = BIOS_IN16(0x48);

	return 0;
}
#endif /* __i386__ */

#ifdef CONFIG_PPC_OF
/*
 * Read XTAL (ref clock), SCLK and MCLK from Open Firmware device
 * tree. Hopefully, ATI OF driver is kind enough to fill these
 */
static int __devinit radeon_read_xtal_OF (struct radeonfb_info *rinfo)
{
	struct device_node *dp;
	u32 *val;

	dp = pci_device_to_OF_node(rinfo->pdev);
	if (dp == NULL) {
		printk(KERN_WARNING "radeonfb: Cannot match card to OF node !\n");
		return -ENODEV;
	}
	val = (u32 *) get_property(dp, "ATY,RefCLK", 0);
	if (!val || !*val) {
		printk(KERN_WARNING "radeonfb: No ATY,RefCLK property !\n");
		return -EINVAL;
	}

	rinfo->pll.ref_clk = (*val) / 10;

	val = (u32 *) get_property(dp, "ATY,SCLK", 0);
	if (val && *val)
		rinfo->pll.sclk = (*val) / 10;

	val = (u32 *) get_property(dp, "ATY,MCLK", 0);
	if (val && *val)
		rinfo->pll.mclk = (*val) / 10;

       	return 0;
}
#endif /* CONFIG_PPC_OF */

/*
 * Read PLL infos from chip registers
 */
static int __devinit radeon_probe_pll_params(struct radeonfb_info *rinfo)
{
	unsigned char ppll_div_sel;
	unsigned Ns, Nm, M;
	unsigned sclk, mclk, tmp, ref_div;
	int hTotal, vTotal, num, denom, m, n;
	unsigned long long hz, vclk;
	long xtal;
	struct timeval start_tv, stop_tv;
	long total_secs, total_usecs;
	int i;

	/* Ugh, we cut interrupts, bad bad bad, but we want some precision
	 * here, so... --BenH
	 */

	/* Flush PCI buffers ? */
	tmp = INREG(DEVICE_ID);

	local_irq_disable();

	for(i=0; i<1000000; i++)
		if (((INREG(CRTC_VLINE_CRNT_VLINE) >> 16) & 0x3ff) == 0)
			break;

	do_gettimeofday(&start_tv);

	for(i=0; i<1000000; i++)
		if (((INREG(CRTC_VLINE_CRNT_VLINE) >> 16) & 0x3ff) != 0)
			break;

	for(i=0; i<1000000; i++)
		if (((INREG(CRTC_VLINE_CRNT_VLINE) >> 16) & 0x3ff) == 0)
			break;
	
	do_gettimeofday(&stop_tv);
	
	local_irq_enable();

	total_secs = stop_tv.tv_sec - start_tv.tv_sec;
	if (total_secs > 10)
		return -1;
	total_usecs = stop_tv.tv_usec - start_tv.tv_usec;
	total_usecs += total_secs * 1000000;
	if (total_usecs < 0)
		total_usecs = -total_usecs;
	hz = 1000000/total_usecs;
 
	hTotal = ((INREG(CRTC_H_TOTAL_DISP) & 0x1ff) + 1) * 8;
	vTotal = ((INREG(CRTC_V_TOTAL_DISP) & 0x3ff) + 1);
	vclk = (long long)hTotal * (long long)vTotal * hz;

	switch((INPLL(PPLL_REF_DIV) & 0x30000) >> 16) {
	case 0:
	default:
		num = 1;
		denom = 1;
		break;
	case 1:
		n = ((INPLL(X_MPLL_REF_FB_DIV) >> 16) & 0xff);
		m = (INPLL(X_MPLL_REF_FB_DIV) & 0xff);
		num = 2*n;
		denom = 2*m;
		break;
	case 2:
		n = ((INPLL(X_MPLL_REF_FB_DIV) >> 8) & 0xff);
		m = (INPLL(X_MPLL_REF_FB_DIV) & 0xff);
		num = 2*n;
		denom = 2*m;
        break;
	}

	OUTREG8(CLOCK_CNTL_INDEX, 1);
	ppll_div_sel = INREG8(CLOCK_CNTL_DATA + 1) & 0x3;

	n = (INPLL(PPLL_DIV_0 + ppll_div_sel) & 0x7ff);
	m = (INPLL(PPLL_REF_DIV) & 0x3ff);

	num *= n;
	denom *= m;

	switch ((INPLL(PPLL_DIV_0 + ppll_div_sel) >> 16) & 0x7) {
	case 1:
		denom *= 2;
		break;
	case 2:
		denom *= 4;
		break;
	case 3:
		denom *= 8;
		break;
	case 4:
		denom *= 3;
		break;
	case 6:
		denom *= 6;   
		break;
	case 7:
		denom *= 12;
		break;
	}

	vclk *= denom;
	do_div(vclk, 1000 * num);
	xtal = vclk;

	if ((xtal > 26900) && (xtal < 27100))
		xtal = 2700;
	else if ((xtal > 14200) && (xtal < 14400))
		xtal = 1432;
	else if ((xtal > 29400) && (xtal < 29600))
		xtal = 2950;
	else {
		printk(KERN_WARNING "xtal calculation failed: %ld\n", xtal);
		return -1;
	}

	tmp = INPLL(X_MPLL_REF_FB_DIV);
	ref_div = INPLL(PPLL_REF_DIV) & 0x3ff;

	Ns = (tmp & 0xff0000) >> 16;
	Nm = (tmp & 0xff00) >> 8;
	M = (tmp & 0xff);
	sclk = round_div((2 * Ns * xtal), (2 * M));
	mclk = round_div((2 * Nm * xtal), (2 * M));

	/* we're done, hopefully these are sane values */
	rinfo->pll.ref_clk = xtal;
	rinfo->pll.ref_div = ref_div;
	rinfo->pll.sclk = sclk;
	rinfo->pll.mclk = mclk;

	return 0;
}

/*
 * Retreive PLL infos by different means (BIOS, Open Firmware, register probing...)
 */
static void __devinit radeon_get_pllinfo(struct radeonfb_info *rinfo)
{
#ifdef CONFIG_PPC_OF
	/*
	 * Retreive PLL infos from Open Firmware first
	 */
       	if (!force_measure_pll && radeon_read_xtal_OF(rinfo) == 0) {
       		printk(KERN_INFO "radeonfb: Retreived PLL infos from Open Firmware\n");
       		rinfo->pll.ref_div = INPLL(PPLL_REF_DIV) & 0x3ff;
		/* FIXME: Max clock may be higher on newer chips */
       		rinfo->pll.ppll_min = 12000;
       		rinfo->pll.ppll_max = 35000;
		goto found;
	}
#endif /* CONFIG_PPC_OF */

	/*
	 * Check out if we have an X86 which gave us some PLL informations
	 * and if yes, retreive them
	 */
	if (!force_measure_pll && rinfo->bios_seg) {
		u16 pll_info_block = BIOS_IN16(rinfo->fp_bios_start + 0x30);

		rinfo->pll.sclk		= BIOS_IN16(pll_info_block + 0x08);
		rinfo->pll.mclk		= BIOS_IN16(pll_info_block + 0x0a);
		rinfo->pll.ref_clk	= BIOS_IN16(pll_info_block + 0x0e);
		rinfo->pll.ref_div	= BIOS_IN16(pll_info_block + 0x10);
		rinfo->pll.ppll_min	= BIOS_IN32(pll_info_block + 0x12);
		rinfo->pll.ppll_max	= BIOS_IN32(pll_info_block + 0x16);

		printk(KERN_INFO "radeonfb: Retreived PLL infos from BIOS\n");
		goto found;
	}

	/*
	 * We didn't get PLL parameters from either OF or BIOS, we try to
	 * probe them
	 */
	if (radeon_probe_pll_params(rinfo) == 0) {
		printk(KERN_INFO "radeonfb: Retreived PLL infos from registers\n");
		/* FIXME: Max clock may be higher on newer chips */
       		rinfo->pll.ppll_min = 12000;
       		rinfo->pll.ppll_max = 35000;
		goto found;
	}

	/*
	 * Neither of the above worked, we have a few default values, though
	 * that's mostly incomplete
	 */
	switch (rinfo->chipset) {
	case PCI_DEVICE_ID_ATI_RADEON_QW:
	case PCI_DEVICE_ID_ATI_RADEON_QX:
		rinfo->pll.ppll_max = 35000;
		rinfo->pll.ppll_min = 12000;
		rinfo->pll.mclk = 23000;
		rinfo->pll.sclk = 23000;
		rinfo->pll.ref_clk = 2700;
		break;
	case PCI_DEVICE_ID_ATI_RADEON_QL:
	case PCI_DEVICE_ID_ATI_RADEON_QN:
	case PCI_DEVICE_ID_ATI_RADEON_QO:
	case PCI_DEVICE_ID_ATI_RADEON_Ql:
	case PCI_DEVICE_ID_ATI_RADEON_BB:
		rinfo->pll.ppll_max = 35000;
		rinfo->pll.ppll_min = 12000;
		rinfo->pll.mclk = 27500;
		rinfo->pll.sclk = 27500;
		rinfo->pll.ref_clk = 2700;
		break;
	case PCI_DEVICE_ID_ATI_RADEON_Id:
	case PCI_DEVICE_ID_ATI_RADEON_Ie:
	case PCI_DEVICE_ID_ATI_RADEON_If:
	case PCI_DEVICE_ID_ATI_RADEON_Ig:
		rinfo->pll.ppll_max = 35000;
		rinfo->pll.ppll_min = 12000;
		rinfo->pll.mclk = 25000;
		rinfo->pll.sclk = 25000;
		rinfo->pll.ref_clk = 2700;
		break;
	case PCI_DEVICE_ID_ATI_RADEON_ND:
	case PCI_DEVICE_ID_ATI_RADEON_NE:
	case PCI_DEVICE_ID_ATI_RADEON_NF:
	case PCI_DEVICE_ID_ATI_RADEON_NG:
		rinfo->pll.ppll_max = 40000;
		rinfo->pll.ppll_min = 20000;
		rinfo->pll.mclk = 27000;
		rinfo->pll.sclk = 27000;
		rinfo->pll.ref_clk = 2700;
		break;
	case PCI_DEVICE_ID_ATI_RADEON_QD:
	case PCI_DEVICE_ID_ATI_RADEON_QE:
	case PCI_DEVICE_ID_ATI_RADEON_QF:
	case PCI_DEVICE_ID_ATI_RADEON_QG:
	default:
		rinfo->pll.ppll_max = 35000;
		rinfo->pll.ppll_min = 12000;
		rinfo->pll.mclk = 16600;
		rinfo->pll.sclk = 16600;
		rinfo->pll.ref_clk = 2700;
		break;
	}
	rinfo->pll.ref_div = INPLL(PPLL_REF_DIV) & 0x3ff;

       	printk(KERN_INFO "radeonfb: Used default PLL infos\n");

found:
	/*
	 * Some methods fail to retreive SCLK and MCLK values, we apply default
	 * settings in this case (200Mhz). If that really happne often, we could
	 * fetch from registers instead...
	 */
	if (rinfo->pll.mclk == 0)
		rinfo->pll.mclk = 20000;
	if (rinfo->pll.sclk == 0)
		rinfo->pll.sclk = 20000;

	printk("radeonfb: Reference=%d.%02d MHz (RefDiv=%d) Memory=%d.%02d Mhz, System=%d.%02d MHz\n",
	       rinfo->pll.ref_clk / 100, rinfo->pll.ref_clk % 100,
	       rinfo->pll.ref_div,
	       rinfo->pll.mclk / 100, rinfo->pll.mclk % 100,
	       rinfo->pll.sclk / 100, rinfo->pll.sclk % 100);
}

static int radeonfb_check_var (struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct radeonfb_info *rinfo = info->par;
        struct fb_var_screeninfo v;
        int nom, den;
	unsigned int pitch;

	if (radeon_match_mode(rinfo, &v, var))
		return -EINVAL;

        switch (v.bits_per_pixel) {
		case 0 ... 8:
			v.bits_per_pixel = 8;
			break;
		case 9 ... 16:
			v.bits_per_pixel = 16;
			break;
		case 17 ... 24:
#if 0 /* Doesn't seem to work */
			v.bits_per_pixel = 24;
			break;
#endif			
			return -EINVAL;
		case 25 ... 32:
			v.bits_per_pixel = 32;
			break;
		default:
			return -EINVAL;
	}

	switch (var_to_depth(&v)) {
                case 8:
                        nom = den = 1;
                        v.red.offset = v.green.offset = v.blue.offset = 0;
                        v.red.length = v.green.length = v.blue.length = 8;
                        v.transp.offset = v.transp.length = 0;
                        break;
		case 15:
			nom = 2;
			den = 1;
			v.red.offset = 10;
			v.green.offset = 5;
			v.blue.offset = 0;
			v.red.length = v.green.length = v.blue.length = 5;
			v.transp.offset = v.transp.length = 0;
			break;
                case 16:
                        nom = 2;
                        den = 1;
                        v.red.offset = 11;
                        v.green.offset = 5;
                        v.blue.offset = 0;
                        v.red.length = 5;
                        v.green.length = 6;
                        v.blue.length = 5;
                        v.transp.offset = v.transp.length = 0;
                        break;                          
                case 24:
                        nom = 4;
                        den = 1;
                        v.red.offset = 16;
                        v.green.offset = 8;
                        v.blue.offset = 0;
                        v.red.length = v.blue.length = v.green.length = 8;
                        v.transp.offset = v.transp.length = 0;
                        break;
                case 32:
                        nom = 4;
                        den = 1;
                        v.red.offset = 16;
                        v.green.offset = 8;
                        v.blue.offset = 0;
                        v.red.length = v.blue.length = v.green.length = 8;
                        v.transp.offset = 24;
                        v.transp.length = 8;
                        break;
                default:
                        printk ("radeonfb: mode %dx%dx%d rejected, color depth invalid\n",
                                var->xres, var->yres, var->bits_per_pixel);
                        return -EINVAL;
        }

	if (v.yres_virtual < v.yres)
		v.yres_virtual = v.yres;
	if (v.xres_virtual < v.xres)
		v.xres_virtual = v.xres;
                

	/* XXX I'm adjusting xres_virtual to the pitch, that may help XFree
	 * with some panels, though I don't quite like this solution
	 */
  	if (radeon_accel_disabled()) {
		v.xres_virtual = v.xres_virtual & ~7ul;
		v.accel_flags = 0;
	} else {
		pitch = ((v.xres_virtual * ((v.bits_per_pixel + 1) / 8) + 0x3f)
 				& ~(0x3f)) >> 6;
		v.xres_virtual = (pitch << 6) / ((v.bits_per_pixel + 1) / 8);
	}

	if (((v.xres_virtual * v.yres_virtual * nom) / den) > rinfo->mapped_vram)
		return -EINVAL;

	if (v.xres_virtual < v.xres)
		v.xres = v.xres_virtual;

	if (v.xoffset < 0)
                v.xoffset = 0;
        if (v.yoffset < 0)
                v.yoffset = 0;
         
        if (v.xoffset > v.xres_virtual - v.xres)
                v.xoffset = v.xres_virtual - v.xres - 1;
                        
        if (v.yoffset > v.yres_virtual - v.yres)
                v.yoffset = v.yres_virtual - v.yres - 1;
         
        v.red.msb_right = v.green.msb_right = v.blue.msb_right =
                          v.transp.offset = v.transp.length =
                          v.transp.msb_right = 0;
	
        memcpy(var, &v, sizeof(v));

        return 0;
}


static int radeonfb_pan_display (struct fb_var_screeninfo *var,
                                 struct fb_info *info)
{
        struct radeonfb_info *rinfo = info->par;

        if ((var->xoffset + var->xres > var->xres_virtual)
	    || (var->yoffset + var->yres > var->yres_virtual))
               return -EINVAL;
                
        if (rinfo->asleep)
        	return 0;

        OUTREG(CRTC_OFFSET, ((var->yoffset * var->xres_virtual + var->xoffset)
			     * var->bits_per_pixel / 8) & ~7);
        return 0;
}


static int radeonfb_ioctl (struct inode *inode, struct file *file, unsigned int cmd,
                           unsigned long arg, struct fb_info *info)
{
        struct radeonfb_info *rinfo = info->par;
	unsigned int tmp;
	u32 value = 0;
	int rc;

	switch (cmd) {
		/*
		 * TODO:  set mirror accordingly for non-Mobility chipsets with 2 CRTC's
		 *        and do something better using 2nd CRTC instead of just hackish
		 *        routing to second output
		 */
		case FBIO_RADEON_SET_MIRROR:
			if (!rinfo->is_mobility)
				return -EINVAL;

			rc = get_user(value, (__u32*)arg);

			if (rc)
				return rc;

			if (value & 0x01) {
				tmp = INREG(LVDS_GEN_CNTL);

				tmp |= (LVDS_ON | LVDS_BLON);
			} else {
				tmp = INREG(LVDS_GEN_CNTL);

				tmp &= ~(LVDS_ON | LVDS_BLON);
			}

			OUTREG(LVDS_GEN_CNTL, tmp);

			if (value & 0x02) {
				tmp = INREG(CRTC_EXT_CNTL);
				tmp |= CRTC_CRT_ON;

				mirror = 1;
			} else {
				tmp = INREG(CRTC_EXT_CNTL);
				tmp &= ~CRTC_CRT_ON;

				mirror = 0;
			}

			OUTREG(CRTC_EXT_CNTL, tmp);

			break;
		case FBIO_RADEON_GET_MIRROR:
			if (!rinfo->is_mobility)
				return -EINVAL;

			tmp = INREG(LVDS_GEN_CNTL);
			if ((LVDS_ON | LVDS_BLON) & tmp)
				value |= 0x01;

			tmp = INREG(CRTC_EXT_CNTL);
			if (CRTC_CRT_ON & tmp)
				value |= 0x02;

			return put_user(value, (__u32*)arg);
		default:
			return -EINVAL;
	}

	return -EINVAL;
}


static int radeon_screen_blank (struct radeonfb_info *rinfo, int blank)
{
        u32 val = INREG(CRTC_EXT_CNTL);
	u32 val2 = 0;

	if (rinfo->mon1_type == MT_LCD)
		val2 = INREG(LVDS_GEN_CNTL) & ~LVDS_DISPLAY_DIS;
	
        /* reset it */
        val &= ~(CRTC_DISPLAY_DIS | CRTC_HSYNC_DIS |
                 CRTC_VSYNC_DIS);

        switch (blank) {
                case VESA_NO_BLANKING:
                        break;
                case VESA_VSYNC_SUSPEND:
                        val |= (CRTC_DISPLAY_DIS | CRTC_VSYNC_DIS);
                        break;
                case VESA_HSYNC_SUSPEND:
                        val |= (CRTC_DISPLAY_DIS | CRTC_HSYNC_DIS);
                        break;
                case VESA_POWERDOWN:
                        val |= (CRTC_DISPLAY_DIS | CRTC_VSYNC_DIS | 
                                CRTC_HSYNC_DIS);
			val2 |= (LVDS_DISPLAY_DIS);
                        break;
        }

	switch (rinfo->mon1_type) {
		case MT_LCD:
			OUTREG(LVDS_GEN_CNTL, val2);
			break;
		case MT_CRT:
		default:
		        OUTREG(CRTC_EXT_CNTL, val);
			break;
	}

	return 0;
}

int radeonfb_blank (int blank, struct fb_info *info)
{
        struct radeonfb_info *rinfo = info->par;

	if (rinfo->asleep)
		return 0;
		
#ifdef CONFIG_PMAC_BACKLIGHT
	if (rinfo->mon1_type == MT_LCD && _machine == _MACH_Pmac && blank)
		set_backlight_enable(0);
#endif
                        
	radeon_screen_blank(rinfo, blank);

#ifdef CONFIG_PMAC_BACKLIGHT
	if (rinfo->mon1_type == MT_LCD && _machine == _MACH_Pmac && !blank)
		set_backlight_enable(1);
#endif

	return 0;
}

static int radeonfb_setcolreg (unsigned regno, unsigned red, unsigned green,
                             unsigned blue, unsigned transp, struct fb_info *info)
{
        struct radeonfb_info *rinfo = info->par;
	u32 pindex;
	unsigned int i;
	
	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;
	rinfo->palette[regno].red = red;
	rinfo->palette[regno].green = green;
	rinfo->palette[regno].blue = blue;

        /* default */
        pindex = regno;

        if (!rinfo->asleep) {
        	u32 dac_cntl2, vclk_cntl = 0;
        	
		if (rinfo->is_mobility) {
			vclk_cntl = INPLL(VCLK_ECP_CNTL);
			OUTPLL(VCLK_ECP_CNTL, vclk_cntl & ~PIXCLK_DAC_ALWAYS_ONb);
		}

		/* Make sure we are on first palette */
		if (rinfo->has_CRTC2) {
			dac_cntl2 = INREG(DAC_CNTL2);
			dac_cntl2 &= ~DAC2_PALETTE_ACCESS_CNTL;
			OUTREG(DAC_CNTL2, dac_cntl2);
		}

		if (rinfo->bpp == 16) {
			pindex = regno * 8;

			if (rinfo->depth == 16 && regno > 63)
				return 1;
			if (rinfo->depth == 15 && regno > 31)
				return 1;

			/* For 565, the green component is mixed one order below */
			if (rinfo->depth == 16) {
		                OUTREG(PALETTE_INDEX, pindex>>1);
	       	         	OUTREG(PALETTE_DATA, (rinfo->palette[regno>>1].red << 16) |
	                        	(green << 8) | (rinfo->palette[regno>>1].blue));
	                	green = rinfo->palette[regno<<1].green;
	        	}
		}

		if (rinfo->depth != 16 || regno < 32) {
			OUTREG(PALETTE_INDEX, pindex);
			OUTREG(PALETTE_DATA, (red << 16) | (green << 8) | blue);
		}
		if (rinfo->is_mobility)
			OUTPLL(VCLK_ECP_CNTL, vclk_cntl);
	}
 	if (regno < 16) {
		u32 *pal = info->pseudo_palette;
        	switch (rinfo->depth) {
		case 15:
			pal[regno] = (regno << 10) | (regno << 5) | regno;
			break;
		case 16:
			pal[regno] = (regno << 11) | (regno << 5) | regno;
			break;
		case 24:
			pal[regno] = (regno << 16) | (regno << 8) | regno;
			break;
		case 32:
			i = (regno << 8) | regno;
			pal[regno] = (i << 16) | i;
			break;
		}
        }
	return 0;
}


static void radeon_save_state (struct radeonfb_info *rinfo, struct radeon_regs *save)
{
	/* CRTC regs */
	save->crtc_gen_cntl = INREG(CRTC_GEN_CNTL);
	save->crtc_ext_cntl = INREG(CRTC_EXT_CNTL);
	save->crtc_more_cntl = INREG(CRTC_MORE_CNTL);
	save->dac_cntl = INREG(DAC_CNTL);
        save->crtc_h_total_disp = INREG(CRTC_H_TOTAL_DISP);
        save->crtc_h_sync_strt_wid = INREG(CRTC_H_SYNC_STRT_WID);
        save->crtc_v_total_disp = INREG(CRTC_V_TOTAL_DISP);
        save->crtc_v_sync_strt_wid = INREG(CRTC_V_SYNC_STRT_WID);
	save->crtc_pitch = INREG(CRTC_PITCH);
	save->surface_cntl = INREG(SURFACE_CNTL);

	/* FP regs */
	save->fp_crtc_h_total_disp = INREG(FP_CRTC_H_TOTAL_DISP);
	save->fp_crtc_v_total_disp = INREG(FP_CRTC_V_TOTAL_DISP);
	save->fp_gen_cntl = INREG(FP_GEN_CNTL);
	save->fp_h_sync_strt_wid = INREG(FP_H_SYNC_STRT_WID);
	save->fp_horz_stretch = INREG(FP_HORZ_STRETCH);
	save->fp_v_sync_strt_wid = INREG(FP_V_SYNC_STRT_WID);
	save->fp_vert_stretch = INREG(FP_VERT_STRETCH);
	save->lvds_gen_cntl = INREG(LVDS_GEN_CNTL);
	save->lvds_pll_cntl = INREG(LVDS_PLL_CNTL);
	save->tmds_crc = INREG(TMDS_CRC);	save->tmds_transmitter_cntl = INREG(TMDS_TRANSMITTER_CNTL);
	save->vclk_ecp_cntl = INPLL(VCLK_ECP_CNTL);
}


static void radeon_write_pll_regs(struct radeonfb_info *rinfo, struct radeon_regs *mode)
{
	int i;

	/* Workaround from XFree */
	if (rinfo->is_mobility) {
	        /* A temporal workaround for the occational blanking on certain laptop panels. 
	           This appears to related to the PLL divider registers (fail to lock?).  
		   It occurs even when all dividers are the same with their old settings.  
	           In this case we really don't need to fiddle with PLL registers. 
	           By doing this we can avoid the blanking problem with some panels.
	        */
		if ((mode->ppll_ref_div == (INPLL(PPLL_REF_DIV) & PPLL_REF_DIV_MASK)) &&
		    (mode->ppll_div_3 == (INPLL(PPLL_DIV_3) &
					  (PPLL_POST3_DIV_MASK | PPLL_FB3_DIV_MASK)))) {
			/* We still have to force a switch to PPLL div 3 thanks to
			 * an XFree86 driver bug which will switch it away in some cases
			 * even when using UseFDev */
			OUTREGP(CLOCK_CNTL_INDEX, PPLL_DIV_SEL_MASK, ~PPLL_DIV_SEL_MASK);
            		return;
		}
	}

	/* Swich VCKL clock input to CPUCLK so it stays fed while PPLL updates*/
	OUTPLLP(VCLK_ECP_CNTL, VCLK_SRC_SEL_CPUCLK, ~VCLK_SRC_SEL_MASK);

	/* Reset PPLL & enable atomic update */
	OUTPLLP(PPLL_CNTL,
		PPLL_RESET | PPLL_ATOMIC_UPDATE_EN | PPLL_VGA_ATOMIC_UPDATE_EN,
		~(PPLL_RESET | PPLL_ATOMIC_UPDATE_EN | PPLL_VGA_ATOMIC_UPDATE_EN));

	/* Switch to PPLL div 3 */
	OUTREGP(CLOCK_CNTL_INDEX, PPLL_DIV_SEL_MASK, ~PPLL_DIV_SEL_MASK);

	/* Set PPLL ref. div */
	if (rinfo->family == CHIP_FAMILY_R300 ||
	    rinfo->family == CHIP_FAMILY_RS300 ||
	    rinfo->family == CHIP_FAMILY_R350 ||
	    rinfo->family == CHIP_FAMILY_RV350) {
		if (mode->ppll_ref_div & R300_PPLL_REF_DIV_ACC_MASK) {
			/* When restoring console mode, use saved PPLL_REF_DIV
			 * setting.
			 */
			OUTPLLP(PPLL_REF_DIV, mode->ppll_ref_div, 0);
		} else {
			/* R300 uses ref_div_acc field as real ref divider */
			OUTPLLP(PPLL_REF_DIV,
				(mode->ppll_ref_div << R300_PPLL_REF_DIV_ACC_SHIFT), 
				~R300_PPLL_REF_DIV_ACC_MASK);
		}
	} else
		OUTPLLP(PPLL_REF_DIV, mode->ppll_ref_div, ~PPLL_REF_DIV_MASK);

	/* Set PPLL divider 3 & post divider*/
	OUTPLLP(PPLL_DIV_3, mode->ppll_div_3, ~PPLL_FB3_DIV_MASK);
	OUTPLLP(PPLL_DIV_3, mode->ppll_div_3, ~PPLL_POST3_DIV_MASK);

	/* Write update */
	while (INPLL(PPLL_REF_DIV) & PPLL_ATOMIC_UPDATE_R)
		;
	OUTPLLP(PPLL_REF_DIV, PPLL_ATOMIC_UPDATE_W, ~PPLL_ATOMIC_UPDATE_W);

	/* Wait read update complete */
	/* FIXME: Certain revisions of R300 can't recover here.  Not sure of
	   the cause yet, but this workaround will mask the problem for now.
	   Other chips usually will pass at the very first test, so the
	   workaround shouldn't have any effect on them. */
	for (i = 0; (i < 10000 && INPLL(PPLL_REF_DIV) & PPLL_ATOMIC_UPDATE_R); i++)
		;
	
	OUTPLL(HTOTAL_CNTL, 0);

	/* Clear reset & atomic update */
	OUTPLLP(PPLL_CNTL, 0,
		~(PPLL_RESET | PPLL_SLEEP | PPLL_ATOMIC_UPDATE_EN | PPLL_VGA_ATOMIC_UPDATE_EN));

	/* We may want some locking ... oh well */
       	msleep(5);

	/* Switch back VCLK source to PPLL */
	OUTPLLP(VCLK_ECP_CNTL, VCLK_SRC_SEL_PPLLCLK, ~VCLK_SRC_SEL_MASK);
}

/*
 * Timer function for delayed LVDS panel power up/down
 */
static void radeon_lvds_timer_func(unsigned long data)
{
	struct radeonfb_info *rinfo = (struct radeonfb_info *)data;

	OUTREG(LVDS_GEN_CNTL, rinfo->pending_lvds_gen_cntl);
	if (rinfo->pending_pixclks_cntl) {
		OUTPLL(PIXCLKS_CNTL, rinfo->pending_pixclks_cntl);
		rinfo->pending_pixclks_cntl = 0;
	}
}

/*
 * Apply a video mode. This will apply the whole register set, including
 * the PLL registers, to the card
 */
static void radeon_write_mode (struct radeonfb_info *rinfo,
                               struct radeon_regs *mode)
{
	int i;
	int primary_mon = PRIMARY_MONITOR(rinfo);

	if (nomodeset)
		return;

	del_timer_sync(&rinfo->lvds_timer);

	radeon_screen_blank(rinfo, VESA_POWERDOWN);

	for (i=0; i<10; i++)
		OUTREG(common_regs[i].reg, common_regs[i].val);

	/* Apply surface registers */
	for (i=0; i<8; i++) {
		OUTREG(SURFACE0_LOWER_BOUND + 0x10*i, mode->surf_lower_bound[i]);
		OUTREG(SURFACE0_UPPER_BOUND + 0x10*i, mode->surf_upper_bound[i]);
		OUTREG(SURFACE0_INFO + 0x10*i, mode->surf_info[i]);
	}

	OUTREG(CRTC_GEN_CNTL, mode->crtc_gen_cntl);
	OUTREGP(CRTC_EXT_CNTL, mode->crtc_ext_cntl,
		~(CRTC_HSYNC_DIS | CRTC_VSYNC_DIS | CRTC_DISPLAY_DIS));
	OUTREG(CRTC_MORE_CNTL, mode->crtc_more_cntl);
	OUTREGP(DAC_CNTL, mode->dac_cntl, DAC_RANGE_CNTL | DAC_BLANKING);
	OUTREG(CRTC_H_TOTAL_DISP, mode->crtc_h_total_disp);
	OUTREG(CRTC_H_SYNC_STRT_WID, mode->crtc_h_sync_strt_wid);
	OUTREG(CRTC_V_TOTAL_DISP, mode->crtc_v_total_disp);
	OUTREG(CRTC_V_SYNC_STRT_WID, mode->crtc_v_sync_strt_wid);
	OUTREG(CRTC_OFFSET, 0);
	OUTREG(CRTC_OFFSET_CNTL, 0);
	OUTREG(CRTC_PITCH, mode->crtc_pitch);
	OUTREG(SURFACE_CNTL, mode->surface_cntl);

	radeon_write_pll_regs(rinfo, mode);

	if ((primary_mon == MT_DFP) || (primary_mon == MT_LCD)) {
		OUTREG(FP_CRTC_H_TOTAL_DISP, mode->fp_crtc_h_total_disp);
		OUTREG(FP_CRTC_V_TOTAL_DISP, mode->fp_crtc_v_total_disp);
		OUTREG(FP_H_SYNC_STRT_WID, mode->fp_h_sync_strt_wid);
		OUTREG(FP_V_SYNC_STRT_WID, mode->fp_v_sync_strt_wid);
		OUTREG(FP_HORZ_STRETCH, mode->fp_horz_stretch);
		OUTREG(FP_VERT_STRETCH, mode->fp_vert_stretch);
		OUTREG(FP_GEN_CNTL, mode->fp_gen_cntl);
		OUTREG(TMDS_CRC, mode->tmds_crc);
		OUTREG(TMDS_TRANSMITTER_CNTL, mode->tmds_transmitter_cntl);

		if (primary_mon == MT_LCD) {
			unsigned int tmp = INREG(LVDS_GEN_CNTL);

			/* HACK: The backlight control code may have modified init_state.lvds_gen_cntl,
			 * so we update ourselves
			 */
			mode->lvds_gen_cntl &= ~LVDS_STATE_MASK;
			mode->lvds_gen_cntl |= (rinfo->init_state.lvds_gen_cntl & LVDS_STATE_MASK);

			if ((tmp & (LVDS_ON | LVDS_BLON)) ==
			    (mode->lvds_gen_cntl & (LVDS_ON | LVDS_BLON))) {
				OUTREG(LVDS_GEN_CNTL, mode->lvds_gen_cntl);
			} else {
				rinfo->pending_pixclks_cntl = INPLL(PIXCLKS_CNTL);
				if (rinfo->is_mobility || rinfo->is_IGP)
					OUTPLLP(PIXCLKS_CNTL, 0, ~PIXCLK_LVDS_ALWAYS_ONb);
				if (!(tmp & (LVDS_ON | LVDS_BLON)))
					OUTREG(LVDS_GEN_CNTL, mode->lvds_gen_cntl | LVDS_BLON);
				rinfo->pending_lvds_gen_cntl = mode->lvds_gen_cntl;
				mod_timer(&rinfo->lvds_timer,
					  jiffies + MS_TO_HZ(rinfo->panel_info.pwr_delay));
			}
		}
	}

	RTRACE("lvds_gen_cntl: %08x\n", INREG(LVDS_GEN_CNTL));

	radeon_screen_blank(rinfo, VESA_NO_BLANKING);

	OUTPLL(VCLK_ECP_CNTL, mode->vclk_ecp_cntl);
	
	return;
}

/*
 * Calculate the PLL values for a given mode
 */
static void radeon_calc_pll_regs(struct radeonfb_info *rinfo, struct radeon_regs *regs,
				 unsigned long freq)
{
	const struct {
		int divider;
		int bitvalue;
	} *post_div,
	  post_divs[] = {
		{ 1,  0 },
		{ 2,  1 },
		{ 4,  2 },
		{ 8,  3 },
		{ 3,  4 },
		{ 16, 5 },
		{ 6,  6 },
		{ 12, 7 },
		{ 0,  0 },
	};
	int fb_div, pll_output_freq = 0;
	int uses_dvo = 0;

	/* Check if the DVO port is enabled and sourced from the primary CRTC. I'm
	 * not sure which model starts having FP2_GEN_CNTL, I assume anything more
	 * recent than an r(v)100...
	 */
#if 0
	/* XXX I had reports of flicker happening with the cinema display
	 * on TMDS1 that seem to be fixed if I also forbit odd dividers in
	 * this case. This could just be a bandwidth calculation issue, I
	 * haven't implemented the bandwidth code yet, but in the meantime,
	 * forcing uses_dvo to 1 fixes it and shouln't have bad side effects,
	 * I haven't seen a case were were absolutely needed an odd PLL
	 * divider. I'll find a better fix once I have more infos on the
	 * real cause of the problem.
	 */
	while (rinfo->has_CRTC2) {
		u32 fp2_gen_cntl = INREG(FP2_GEN_CNTL);
		u32 disp_output_cntl;
		int source;

		/* FP2 path not enabled */
		if ((fp2_gen_cntl & FP2_ON) == 0)
			break;
		/* Not all chip revs have the same format for this register,
		 * extract the source selection
		 */
		if (rinfo->family == CHIP_FAMILY_R200 ||
		    rinfo->family == CHIP_FAMILY_R300 ||
		    rinfo->family == CHIP_FAMILY_R350 ||
		    rinfo->family == CHIP_FAMILY_RV350) {
			source = (fp2_gen_cntl >> 10) & 0x3;
			/* sourced from transform unit, check for transform unit
			 * own source
			 */
			if (source == 3) {
				disp_output_cntl = INREG(DISP_OUTPUT_CNTL);
				source = (disp_output_cntl >> 12) & 0x3;
			}
		} else
			source = (fp2_gen_cntl >> 13) & 0x1;
		/* sourced from CRTC2 -> exit */
		if (source == 1)
			break;

		/* so we end up on CRTC1, let's set uses_dvo to 1 now */
		uses_dvo = 1;
		break;
	}
#else
	uses_dvo = 1;
#endif
	if (freq > rinfo->pll.ppll_max)
		freq = rinfo->pll.ppll_max;
	if (freq*12 < rinfo->pll.ppll_min)
		freq = rinfo->pll.ppll_min / 12;

	for (post_div = &post_divs[0]; post_div->divider; ++post_div) {
		pll_output_freq = post_div->divider * freq;
		/* If we output to the DVO port (external TMDS), we don't allow an
		 * odd PLL divider as those aren't supported on this path
		 */
		if (uses_dvo && (post_div->divider & 1))
			continue;
		if (pll_output_freq >= rinfo->pll.ppll_min  &&
		    pll_output_freq <= rinfo->pll.ppll_max)
			break;
	}

	fb_div = round_div(rinfo->pll.ref_div*pll_output_freq,
				  rinfo->pll.ref_clk);
	regs->ppll_ref_div = rinfo->pll.ref_div;
	regs->ppll_div_3 = fb_div | (post_div->bitvalue << 16);

	RTRACE("post div = 0x%x\n", post_div->bitvalue);
	RTRACE("fb_div = 0x%x\n", fb_div);
	RTRACE("ppll_div_3 = 0x%x\n", regs->ppll_div_3);
}

int radeonfb_set_par(struct fb_info *info)
{
	struct radeonfb_info *rinfo = info->par;
	struct fb_var_screeninfo *mode = &info->var;
	struct radeon_regs newmode;
	int hTotal, vTotal, hSyncStart, hSyncEnd,
	    hSyncPol, vSyncStart, vSyncEnd, vSyncPol, cSync;
	u8 hsync_adj_tab[] = {0, 0x12, 9, 9, 6, 5};
	u8 hsync_fudge_fp[] = {2, 2, 0, 0, 5, 5};
	u32 sync, h_sync_pol, v_sync_pol, dotClock, pixClock;
	int i, freq;
        int format = 0;
	int nopllcalc = 0;
	int hsync_start, hsync_fudge, bytpp, hsync_wid, vsync_wid;
	int primary_mon = PRIMARY_MONITOR(rinfo);
	int depth = var_to_depth(mode);

	/* We always want engine to be idle on a mode switch, even
	 * if we won't actually change the mode
	 */
	radeon_engine_idle();

	hSyncStart = mode->xres + mode->right_margin;
	hSyncEnd = hSyncStart + mode->hsync_len;
	hTotal = hSyncEnd + mode->left_margin;

	vSyncStart = mode->yres + mode->lower_margin;
	vSyncEnd = vSyncStart + mode->vsync_len;
	vTotal = vSyncEnd + mode->upper_margin;
	pixClock = mode->pixclock;

	sync = mode->sync;
	h_sync_pol = sync & FB_SYNC_HOR_HIGH_ACT ? 0 : 1;
	v_sync_pol = sync & FB_SYNC_VERT_HIGH_ACT ? 0 : 1;

	if (primary_mon == MT_DFP || primary_mon == MT_LCD) {
		if (rinfo->panel_info.xres < mode->xres)
			mode->xres = rinfo->panel_info.xres;
		if (rinfo->panel_info.yres < mode->yres)
			mode->yres = rinfo->panel_info.yres;

		hTotal = mode->xres + rinfo->panel_info.hblank;
		hSyncStart = mode->xres + rinfo->panel_info.hOver_plus;
		hSyncEnd = hSyncStart + rinfo->panel_info.hSync_width;

		vTotal = mode->yres + rinfo->panel_info.vblank;
		vSyncStart = mode->yres + rinfo->panel_info.vOver_plus;
		vSyncEnd = vSyncStart + rinfo->panel_info.vSync_width;

		h_sync_pol = !rinfo->panel_info.hAct_high;
		v_sync_pol = !rinfo->panel_info.vAct_high;

		pixClock = 100000000 / rinfo->panel_info.clock;

		if (rinfo->panel_info.use_bios_dividers) {
			nopllcalc = 1;
			newmode.ppll_div_3 = rinfo->panel_info.fbk_divider |
				(rinfo->panel_info.post_divider << 16);
			newmode.ppll_ref_div = rinfo->panel_info.ref_divider;
		}
	}
	dotClock = 1000000000 / pixClock;
	freq = dotClock / 10; /* x100 */

	RTRACE("hStart = %d, hEnd = %d, hTotal = %d\n",
		hSyncStart, hSyncEnd, hTotal);
	RTRACE("vStart = %d, vEnd = %d, vTotal = %d\n",
		vSyncStart, vSyncEnd, vTotal);

	hsync_wid = (hSyncEnd - hSyncStart) / 8;
	vsync_wid = vSyncEnd - vSyncStart;
	if (hsync_wid == 0)
		hsync_wid = 1;
	else if (hsync_wid > 0x3f)	/* max */
		hsync_wid = 0x3f;

	if (vsync_wid == 0)
		vsync_wid = 1;
	else if (vsync_wid > 0x1f)	/* max */
		vsync_wid = 0x1f;

	hSyncPol = mode->sync & FB_SYNC_HOR_HIGH_ACT ? 0 : 1;
	vSyncPol = mode->sync & FB_SYNC_VERT_HIGH_ACT ? 0 : 1;

	cSync = mode->sync & FB_SYNC_COMP_HIGH_ACT ? (1 << 4) : 0;

	format = radeon_get_dstbpp(depth);
	bytpp = mode->bits_per_pixel >> 3;

	if ((primary_mon == MT_DFP) || (primary_mon == MT_LCD))
		hsync_fudge = hsync_fudge_fp[format-1];
	else
		hsync_fudge = hsync_adj_tab[format-1];

	hsync_start = hSyncStart - 8 + hsync_fudge;

	newmode.crtc_gen_cntl = CRTC_EXT_DISP_EN | CRTC_EN |
				(format << 8);

	/* Clear auto-center etc... */
	newmode.crtc_more_cntl = rinfo->init_state.crtc_more_cntl;
	newmode.crtc_more_cntl &= 0xfffffff0;
	
	if ((primary_mon == MT_DFP) || (primary_mon == MT_LCD)) {
		newmode.crtc_ext_cntl = VGA_ATI_LINEAR | XCRT_CNT_EN;
		if (mirror)
			newmode.crtc_ext_cntl |= CRTC_CRT_ON;

		newmode.crtc_gen_cntl &= ~(CRTC_DBL_SCAN_EN |
					   CRTC_INTERLACE_EN);
	} else {
		newmode.crtc_ext_cntl = VGA_ATI_LINEAR | XCRT_CNT_EN |
					CRTC_CRT_ON;
	}

	newmode.dac_cntl = /* INREG(DAC_CNTL) | */ DAC_MASK_ALL | DAC_VGA_ADR_EN |
			   DAC_8BIT_EN;

	newmode.crtc_h_total_disp = ((((hTotal / 8) - 1) & 0x3ff) |
				     (((mode->xres / 8) - 1) << 16));

	newmode.crtc_h_sync_strt_wid = ((hsync_start & 0x1fff) |
					(hsync_wid << 16) | (h_sync_pol << 23));

	newmode.crtc_v_total_disp = ((vTotal - 1) & 0xffff) |
				    ((mode->yres - 1) << 16);

	newmode.crtc_v_sync_strt_wid = (((vSyncStart - 1) & 0xfff) |
					 (vsync_wid << 16) | (v_sync_pol  << 23));

	if (!radeon_accel_disabled()) {
		/* We first calculate the engine pitch */
		rinfo->pitch = ((mode->xres_virtual * ((mode->bits_per_pixel + 1) / 8) + 0x3f)
 				& ~(0x3f)) >> 6;

		/* Then, re-multiply it to get the CRTC pitch */
		newmode.crtc_pitch = (rinfo->pitch << 3) / ((mode->bits_per_pixel + 1) / 8);
	} else
		newmode.crtc_pitch = (mode->xres_virtual >> 3);

	newmode.crtc_pitch |= (newmode.crtc_pitch << 16);

	/*
	 * It looks like recent chips have a problem with SURFACE_CNTL,
	 * setting SURF_TRANSLATION_DIS completely disables the
	 * swapper as well, so we leave it unset now.
	 */
	newmode.surface_cntl = 0;

#if defined(__BIG_ENDIAN)

	/* Setup swapping on both apertures, though we currently
	 * only use aperture 0, enabling swapper on aperture 1
	 * won't harm
	 */
	switch (mode->bits_per_pixel) {
		case 16:
			newmode.surface_cntl |= NONSURF_AP0_SWP_16BPP;
			newmode.surface_cntl |= NONSURF_AP1_SWP_16BPP;
			break;
		case 24:	
		case 32:
			newmode.surface_cntl |= NONSURF_AP0_SWP_32BPP;
			newmode.surface_cntl |= NONSURF_AP1_SWP_32BPP;
			break;
	}
#endif

	/* Clear surface registers */
	for (i=0; i<8; i++) {
		newmode.surf_lower_bound[i] = 0;
		newmode.surf_upper_bound[i] = 0x1f;
		newmode.surf_info[i] = 0;
	}

	RTRACE("h_total_disp = 0x%x\t   hsync_strt_wid = 0x%x\n",
		newmode.crtc_h_total_disp, newmode.crtc_h_sync_strt_wid);
	RTRACE("v_total_disp = 0x%x\t   vsync_strt_wid = 0x%x\n",
		newmode.crtc_v_total_disp, newmode.crtc_v_sync_strt_wid);

	rinfo->bpp = mode->bits_per_pixel;
	rinfo->depth = depth;

	RTRACE("pixclock = %lu\n", (unsigned long)pixClock);
	RTRACE("freq = %lu\n", (unsigned long)freq);

	if (!nopllcalc)
		radeon_calc_pll_regs(rinfo, &newmode, freq);

	newmode.vclk_ecp_cntl = rinfo->init_state.vclk_ecp_cntl;

	if ((primary_mon == MT_DFP) || (primary_mon == MT_LCD)) {
		unsigned int hRatio, vRatio;

		if (mode->xres > rinfo->panel_info.xres)
			mode->xres = rinfo->panel_info.xres;
		if (mode->yres > rinfo->panel_info.yres)
			mode->yres = rinfo->panel_info.yres;

		newmode.fp_horz_stretch = (((rinfo->panel_info.xres / 8) - 1)
					   << HORZ_PANEL_SHIFT);
		newmode.fp_vert_stretch = ((rinfo->panel_info.yres - 1)
					   << VERT_PANEL_SHIFT);

		if (mode->xres != rinfo->panel_info.xres) {
			hRatio = round_div(mode->xres * HORZ_STRETCH_RATIO_MAX,
					   rinfo->panel_info.xres);
			newmode.fp_horz_stretch = (((((unsigned long)hRatio) & HORZ_STRETCH_RATIO_MASK)) |
						   (newmode.fp_horz_stretch &
						    (HORZ_PANEL_SIZE | HORZ_FP_LOOP_STRETCH |
						     HORZ_AUTO_RATIO_INC)));
			newmode.fp_horz_stretch |= (HORZ_STRETCH_BLEND |
						    HORZ_STRETCH_ENABLE);
		}
		newmode.fp_horz_stretch &= ~HORZ_AUTO_RATIO;

		if (mode->yres != rinfo->panel_info.yres) {
			vRatio = round_div(mode->yres * VERT_STRETCH_RATIO_MAX,
					   rinfo->panel_info.yres);
			newmode.fp_vert_stretch = (((((unsigned long)vRatio) & VERT_STRETCH_RATIO_MASK)) |
						   (newmode.fp_vert_stretch &
						   (VERT_PANEL_SIZE | VERT_STRETCH_RESERVED)));
			newmode.fp_vert_stretch |= (VERT_STRETCH_BLEND |
						    VERT_STRETCH_ENABLE);
		}
		newmode.fp_vert_stretch &= ~VERT_AUTO_RATIO_EN;

		newmode.fp_gen_cntl = (rinfo->init_state.fp_gen_cntl & (u32)
				       ~(FP_SEL_CRTC2 |
					 FP_RMX_HVSYNC_CONTROL_EN |
					 FP_DFP_SYNC_SEL |
					 FP_CRT_SYNC_SEL |
					 FP_CRTC_LOCK_8DOT |
					 FP_USE_SHADOW_EN |
					 FP_CRTC_USE_SHADOW_VEND |
					 FP_CRT_SYNC_ALT));

		newmode.fp_gen_cntl |= (FP_CRTC_DONT_SHADOW_VPAR |
					FP_CRTC_DONT_SHADOW_HEND);

		newmode.lvds_gen_cntl = rinfo->init_state.lvds_gen_cntl;
		newmode.lvds_pll_cntl = rinfo->init_state.lvds_pll_cntl;
		newmode.tmds_crc = rinfo->init_state.tmds_crc;
		newmode.tmds_transmitter_cntl = rinfo->init_state.tmds_transmitter_cntl;

		if (primary_mon == MT_LCD) {
			newmode.lvds_gen_cntl |= (LVDS_ON | LVDS_BLON);
			newmode.fp_gen_cntl &= ~(FP_FPON | FP_TMDS_EN);
		} else {
			/* DFP */
			newmode.fp_gen_cntl |= (FP_FPON | FP_TMDS_EN);
			newmode.tmds_transmitter_cntl = (TMDS_RAN_PAT_RST | TMDS_ICHCSEL) &
							 ~(TMDS_PLLRST);
			/* TMDS_PLL_EN bit is reversed on RV (and mobility) chips */
			if ((rinfo->family == CHIP_FAMILY_R300) ||
			    (rinfo->family == CHIP_FAMILY_R350) ||
			    (rinfo->family == CHIP_FAMILY_RV350) ||
			    (rinfo->family == CHIP_FAMILY_R200) || !rinfo->has_CRTC2)
				newmode.tmds_transmitter_cntl &= ~TMDS_PLL_EN;
			else
				newmode.tmds_transmitter_cntl |= TMDS_PLL_EN;
			newmode.crtc_ext_cntl &= ~CRTC_CRT_ON;
		}

		newmode.fp_crtc_h_total_disp = (((rinfo->panel_info.hblank / 8) & 0x3ff) |
				(((mode->xres / 8) - 1) << 16));
		newmode.fp_crtc_v_total_disp = (rinfo->panel_info.vblank & 0xffff) |
				((mode->yres - 1) << 16);
		newmode.fp_h_sync_strt_wid = ((rinfo->panel_info.hOver_plus & 0x1fff) |
				(hsync_wid << 16) | (h_sync_pol << 23));
		newmode.fp_v_sync_strt_wid = ((rinfo->panel_info.vOver_plus & 0xfff) |
				(vsync_wid << 16) | (v_sync_pol  << 23));
	}

	/* do it! */
	if (!rinfo->asleep) {
		radeon_write_mode (rinfo, &newmode);
		/* (re)initialize the engine */
		if (!radeon_accel_disabled())
			radeonfb_engine_init (rinfo);
	
	}
	/* Update fix */
	if (!radeon_accel_disabled())
        	info->fix.line_length = rinfo->pitch*64;
        else
		info->fix.line_length = mode->xres_virtual
			* ((mode->bits_per_pixel + 1) / 8);
        info->fix.visual = rinfo->depth == 8 ? FB_VISUAL_PSEUDOCOLOR
		: FB_VISUAL_DIRECTCOLOR;

#ifdef CONFIG_BOOTX_TEXT
	/* Update debug text engine */
	btext_update_display(rinfo->fb_base_phys, mode->xres, mode->yres,
			     rinfo->depth, info->fix.line_length);
#endif

	return 0;
}



static ssize_t radeonfb_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];
	struct radeonfb_info *rinfo = info->par;
	
	if (p >= rinfo->mapped_vram)
	    return 0;
	if (count >= rinfo->mapped_vram)
	    count = rinfo->mapped_vram;
	if (count + p > rinfo->mapped_vram)
		count = rinfo->mapped_vram - p;
	radeonfb_sync(info);
	if (count) {
	    char *base_addr;

	    base_addr = info->screen_base;
	    count -= copy_to_user(buf, base_addr+p, count);
	    if (!count)
		return -EFAULT;
	    *ppos += count;
	}
	return count;
}

static ssize_t radeonfb_write(struct file *file, const char *buf, size_t count,
			      loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];
	struct radeonfb_info *rinfo = info->par;
	int err;

	if (p > rinfo->mapped_vram)
	    return -ENOSPC;
	if (count >= rinfo->mapped_vram)
	    count = rinfo->mapped_vram;
	err = 0;
	if (count + p > rinfo->mapped_vram) {
	    count = rinfo->mapped_vram - p;
	    err = -ENOSPC;
	}
	radeonfb_sync(info);
	if (count) {
	    char *base_addr;

	    base_addr = info->screen_base;
	    count -= copy_from_user(base_addr+p, buf, count);
	    *ppos += count;
	    err = -EFAULT;
	}
	if (count)
		return count;
	return err;
}


static struct fb_ops radeonfb_ops = {
	.owner			= THIS_MODULE,
	.fb_check_var		= radeonfb_check_var,
	.fb_set_par		= radeonfb_set_par,
	.fb_setcolreg		= radeonfb_setcolreg,
	.fb_pan_display 	= radeonfb_pan_display,
	.fb_blank		= radeonfb_blank,
	.fb_ioctl		= radeonfb_ioctl,
	.fb_sync		= radeonfb_sync,
	.fb_fillrect		= radeonfb_fillrect,
	.fb_copyarea		= radeonfb_copyarea,
	.fb_imageblit		= radeonfb_imageblit,
	.fb_read		= radeonfb_read,
	.fb_write		= radeonfb_write,
	.fb_cursor		= soft_cursor,
};


static int __devinit radeon_set_fbinfo (struct radeonfb_info *rinfo)
{
	struct fb_info *info = rinfo->info;

	info->currcon = -1;
	info->par = rinfo;
	info->pseudo_palette = rinfo->pseudo_palette;
        info->flags = FBINFO_FLAG_DEFAULT;
        info->fbops = &radeonfb_ops;
        info->screen_base = (char *)rinfo->fb_base;

	/* Fill fix common fields */
	strlcpy(info->fix.id, rinfo->name, sizeof(info->fix.id));
        info->fix.smem_start = rinfo->fb_base_phys;
        info->fix.smem_len = rinfo->video_ram;
        info->fix.type = FB_TYPE_PACKED_PIXELS;
        info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
        info->fix.xpanstep = 8;
        info->fix.ypanstep = 1;
        info->fix.ywrapstep = 0;
        info->fix.type_aux = 0;
        info->fix.mmio_start = rinfo->mmio_base_phys;
        info->fix.mmio_len = RADEON_REGSIZE;
	if (radeon_accel_disabled())
	        info->fix.accel = FB_ACCEL_NONE;
	else
		info->fix.accel = FB_ACCEL_ATI_RADEON;

	fb_alloc_cmap(&info->cmap, 256, 0);

	if (radeon_accel_disabled())
		info->var.accel_flags &= ~FB_ACCELF_TEXT;
	else
		info->var.accel_flags |= FB_ACCELF_TEXT;

        return 0;
}


#ifdef CONFIG_PMAC_BACKLIGHT

/* TODO: Dbl check these tables, we don't go up to full ON backlight
 * in these, possibly because we noticed MacOS doesn't, but I'd prefer
 * having some more official numbers from ATI
 */
static int backlight_conv_m6[] = {
	0xff, 0xc0, 0xb5, 0xaa, 0x9f, 0x94, 0x89, 0x7e,
	0x73, 0x68, 0x5d, 0x52, 0x47, 0x3c, 0x31, 0x24
};
static int backlight_conv_m7[] = {
	0x00, 0x3f, 0x4a, 0x55, 0x60, 0x6b, 0x76, 0x81,
	0x8c, 0x97, 0xa2, 0xad, 0xb8, 0xc3, 0xce, 0xd9
};

#define BACKLIGHT_LVDS_OFF
#undef BACKLIGHT_DAC_OFF

/* We turn off the LCD completely instead of just dimming the backlight.
 * This provides some greater power saving and the display is useless
 * without backlight anyway.
 */
static int radeon_set_backlight_enable(int on, int level, void *data)
{
	struct radeonfb_info *rinfo = (struct radeonfb_info *)data;
	unsigned int lvds_gen_cntl = INREG(LVDS_GEN_CNTL);
	unsigned long tmpPixclksCntl = INPLL(PIXCLKS_CNTL);
	int* conv_table;

	if (rinfo->mon1_type != MT_LCD)
		return 0;

	/* Pardon me for that hack... maybe some day we can figure
	 * out in what direction backlight should work on a given
	 * panel ?
	 */
	if ((rinfo->family == CHIP_FAMILY_RV200 ||
	     rinfo->family == CHIP_FAMILY_RV250 ||
	     rinfo->family == CHIP_FAMILY_RV280 ||
	     rinfo->family == CHIP_FAMILY_RV350) &&
	    !machine_is_compatible("PowerBook4,3") &&
	    !machine_is_compatible("PowerBook6,3") &&
	    !machine_is_compatible("PowerBook6,5"))
		conv_table = backlight_conv_m7;
	else
		conv_table = backlight_conv_m6;

	del_timer_sync(&rinfo->lvds_timer);

	lvds_gen_cntl |= (LVDS_BL_MOD_EN | LVDS_BLON);
	if (on && (level > BACKLIGHT_OFF)) {
		lvds_gen_cntl |= LVDS_DIGON;
		if (!(lvds_gen_cntl & LVDS_ON)) {
			lvds_gen_cntl &= ~LVDS_BLON;
			OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
			(void)INREG(LVDS_GEN_CNTL);
			mdelay(rinfo->panel_info.pwr_delay);/* OUCH !!! FIXME */
			lvds_gen_cntl |= LVDS_BLON;
			OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
		}
		lvds_gen_cntl &= ~LVDS_BL_MOD_LEVEL_MASK;
		lvds_gen_cntl |= (conv_table[level] <<
				  LVDS_BL_MOD_LEVEL_SHIFT);
		lvds_gen_cntl |= (LVDS_ON | LVDS_EN);
		lvds_gen_cntl &= ~LVDS_DISPLAY_DIS;
	} else {
		/* Asic bug, when turning off LVDS_ON, we have to make sure
		   RADEON_PIXCLK_LVDS_ALWAYS_ON bit is off
		*/
		if (rinfo->is_mobility || rinfo->is_IGP)
			OUTPLLP(PIXCLKS_CNTL, 0, ~PIXCLK_LVDS_ALWAYS_ONb);
		lvds_gen_cntl &= ~LVDS_BL_MOD_LEVEL_MASK;
		lvds_gen_cntl |= (conv_table[0] <<
				  LVDS_BL_MOD_LEVEL_SHIFT);
		lvds_gen_cntl |= LVDS_DISPLAY_DIS | LVDS_BLON;
		OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
		mdelay(rinfo->panel_info.pwr_delay);/* OUCH !!! FIXME */
		lvds_gen_cntl &= ~(LVDS_ON | LVDS_EN | LVDS_BLON | LVDS_DIGON);
	}

	OUTREG(LVDS_GEN_CNTL, lvds_gen_cntl);
	if (rinfo->is_mobility || rinfo->is_IGP)
		OUTPLL(PIXCLKS_CNTL, tmpPixclksCntl);
	rinfo->init_state.lvds_gen_cntl &= ~LVDS_STATE_MASK;
	rinfo->init_state.lvds_gen_cntl |= (lvds_gen_cntl & LVDS_STATE_MASK);

	return 0;
}


static int radeon_set_backlight_level(int level, void *data)
{
	return radeon_set_backlight_enable(1, level, data);
}
#endif /* CONFIG_PMAC_BACKLIGHT */


/*
 * This reconfigure the card's internal memory map. In theory, we'd like
 * to setup the card's memory at the same address as it's PCI bus address,
 * and the AGP aperture right after that so that system RAM on 32 bits
 * machines at least, is directly accessible. However, doing so would
 * conflict with the current XFree drivers...
 * Ultimately, I hope XFree, GATOS and ATI binary drivers will all agree
 * on the proper way to set this up and duplicate this here. In the meantime,
 * I put the card's memory at 0 in card space and AGP at some random high
 * local (0xe0000000 for now) that will be changed by XFree/DRI anyway
 */
#ifdef CONFIG_PPC_OF
#undef SET_MC_FB_FROM_APERTURE
static void fixup_memory_mappings(struct radeonfb_info *rinfo)
{
	u32 save_crtc_gen_cntl, save_crtc2_gen_cntl;
	u32 save_crtc_ext_cntl;
	u32 aper_base, aper_size;
	u32 agp_base;

	/* First, we disable display to avoid interfering */
	if (rinfo->has_CRTC2) {
		save_crtc2_gen_cntl = INREG(CRTC2_GEN_CNTL);
		OUTREG(CRTC2_GEN_CNTL, save_crtc2_gen_cntl | CRTC2_DISP_REQ_EN_B);
	}
	save_crtc_gen_cntl = INREG(CRTC_GEN_CNTL);
	save_crtc_ext_cntl = INREG(CRTC_EXT_CNTL);
	
	OUTREG(CRTC_EXT_CNTL, save_crtc_ext_cntl | CRTC_DISPLAY_DIS);
	OUTREG(CRTC_GEN_CNTL, save_crtc_gen_cntl | CRTC_DISP_REQ_EN_B);
	mdelay(100);

	aper_base = INREG(CONFIG_APER_0_BASE);
	aper_size = INREG(CONFIG_APER_SIZE);

#ifdef SET_MC_FB_FROM_APERTURE
	/* Set framebuffer to be at the same address as set in PCI BAR */
	OUTREG(MC_FB_LOCATION, 
		((aper_base + aper_size - 1) & 0xffff0000) | (aper_base >> 16));
	rinfo->fb_local_base = aper_base;
#else
	OUTREG(MC_FB_LOCATION, 0x7fff0000);
	rinfo->fb_local_base = 0;
#endif
	agp_base = aper_base + aper_size;
	if (agp_base & 0xf0000000)
		agp_base = (aper_base | 0x0fffffff) + 1;

	/* Set AGP to be just after the framebuffer on a 256Mb boundary. This
	 * assumes the FB isn't mapped to 0xf0000000 or above, but this is
	 * always the case on PPCs afaik.
	 */
#ifdef SET_MC_FB_FROM_APERTURE
	OUTREG(MC_AGP_LOCATION, 0xffff0000 | (agp_base >> 16));
#else
	OUTREG(MC_AGP_LOCATION, 0xffffe000);
#endif

	/* Fixup the display base addresses & engine offsets while we
	 * are at it as well
	 */
#ifdef SET_MC_FB_FROM_APERTURE
	OUTREG(DISPLAY_BASE_ADDR, aper_base);
	if (rinfo->has_CRTC2)
		OUTREG(CRTC2_DISPLAY_BASE_ADDR, aper_base);
#else
	OUTREG(DISPLAY_BASE_ADDR, 0);
	if (rinfo->has_CRTC2)
		OUTREG(CRTC2_DISPLAY_BASE_ADDR, 0);
#endif
	mdelay(100);

	/* Restore display settings */
	OUTREG(CRTC_GEN_CNTL, save_crtc_gen_cntl);
	OUTREG(CRTC_EXT_CNTL, save_crtc_ext_cntl);
	if (rinfo->has_CRTC2)
		OUTREG(CRTC2_GEN_CNTL, save_crtc2_gen_cntl);	

	RTRACE("aper_base: %08x MC_FB_LOC to: %08x, MC_AGP_LOC to: %08x\n",
		aper_base,
		((aper_base + aper_size - 1) & 0xffff0000) | (aper_base >> 16),
		0xffff0000 | (agp_base >> 16));
}
#endif /* CONFIG_PPC_OF */


/*
 * Sysfs
 */

static ssize_t radeon_show_one_edid(char *buf, loff_t off, size_t count, const u8 *edid)
{
	if (off > EDID_LENGTH)
		return 0;

	if (off + count > EDID_LENGTH)
		count = EDID_LENGTH - off;

	memcpy(buf, edid + off, count);

	return count;
}


static ssize_t radeon_show_edid1(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct pci_dev *pdev = to_pci_dev(dev);
        struct fb_info *info = pci_get_drvdata(pdev);
        struct radeonfb_info *rinfo = info->par;

	return radeon_show_one_edid(buf, off, count, rinfo->mon1_EDID);
}


static ssize_t radeon_show_edid2(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct pci_dev *pdev = to_pci_dev(dev);
        struct fb_info *info = pci_get_drvdata(pdev);
        struct radeonfb_info *rinfo = info->par;

	return radeon_show_one_edid(buf, off, count, rinfo->mon2_EDID);
}

static struct bin_attribute edid1_attr = {
	.attr   = {
		.name	= "edid1",
		.owner	= THIS_MODULE,
		.mode	= 0444,
	},
	.size	= EDID_LENGTH,
	.read	= radeon_show_edid1,
};

static struct bin_attribute edid2_attr = {
	.attr   = {
		.name	= "edid2",
		.owner	= THIS_MODULE,
		.mode	= 0444,
	},
	.size	= EDID_LENGTH,
	.read	= radeon_show_edid2,
};


static int radeonfb_pci_register (struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct fb_info *info;
	struct radeonfb_info *rinfo;
	u32 tmp;

	RTRACE("radeonfb_pci_register BEGIN\n");
	
	/* Enable device in PCI config */
	if (pci_enable_device(pdev) != 0) {
		printk(KERN_ERR "radeonfb: Cannot enable PCI device\n");
		return -ENODEV;
	}

	info = framebuffer_alloc(sizeof(struct radeonfb_info), &pdev->dev);
	if (!info) {
		printk (KERN_ERR "radeonfb: could not allocate memory\n");
		return -ENODEV;
	}
	rinfo = info->par;
	rinfo->info = info;	
	rinfo->pdev = pdev;
	
	spin_lock_init(&rinfo->reg_lock);
	init_timer(&rinfo->lvds_timer);
	rinfo->lvds_timer.function = radeon_lvds_timer_func;
	rinfo->lvds_timer.data = (unsigned long)rinfo;

	strcpy(rinfo->name, "ATI Radeon XX ");
	rinfo->name[11] = ent->device >> 8;
	rinfo->name[12] = ent->device & 0xFF;
	rinfo->family = ent->driver_data & CHIP_FAMILY_MASK;
	rinfo->chipset = pdev->device;
	rinfo->has_CRTC2 = (ent->driver_data & CHIP_HAS_CRTC2) != 0;
	rinfo->is_mobility = (ent->driver_data & CHIP_IS_MOBILITY) != 0;
	rinfo->is_IGP = (ent->driver_data & CHIP_IS_IGP) != 0;
		
	/* Set base addrs */
	rinfo->fb_base_phys = pci_resource_start (pdev, 0);
	rinfo->mmio_base_phys = pci_resource_start (pdev, 2);

	/* request the mem regions */
	if (!request_mem_region (rinfo->fb_base_phys,
				 pci_resource_len(pdev, 0), "radeonfb")) {
		printk (KERN_ERR "radeonfb: cannot reserve FB region\n");
		goto free_rinfo;
	}

	if (!request_mem_region (rinfo->mmio_base_phys,
				 pci_resource_len(pdev, 2), "radeonfb")) {
		printk (KERN_ERR "radeonfb: cannot reserve MMIO region\n");
		goto release_fb;
	}

	/* map the regions */
	rinfo->mmio_base = (unsigned long) ioremap (rinfo->mmio_base_phys, RADEON_REGSIZE);
	if (!rinfo->mmio_base) {
		printk (KERN_ERR "radeonfb: cannot map MMIO\n");
		goto release_mmio;
	}

	/* On PPC, the firmware sets up a memory mapping that tends
	 * to cause lockups when enabling the engine. We reconfigure
	 * the card internal memory mappings properly
	 */
#ifdef CONFIG_PPC_OF
	fixup_memory_mappings(rinfo);
#else	
	rinfo->fb_local_base = INREG(MC_FB_LOCATION) << 16;
#endif /* CONFIG_PPC_OF */

	/* framebuffer size */
        if ((rinfo->family == CHIP_FAMILY_RS100) ||
            (rinfo->family == CHIP_FAMILY_RS200) ||
            (rinfo->family == CHIP_FAMILY_RS300)) {
          u32 tom = INREG(NB_TOM);
          tmp = ((((tom >> 16) - (tom & 0xffff) + 1) << 6) * 1024);
 
          OUTREG(MC_FB_LOCATION, tom);
          OUTREG(DISPLAY_BASE_ADDR, (tom & 0xffff) << 16);
          OUTREG(CRTC2_DISPLAY_BASE_ADDR, (tom & 0xffff) << 16);
          OUTREG(OV0_BASE_ADDR, (tom & 0xffff) << 16);
 
          /* This is supposed to fix the crtc2 noise problem. */
          OUTREG(GRPH2_BUFFER_CNTL, INREG(GRPH2_BUFFER_CNTL) & ~0x7f0000);
 
          if ((rinfo->family == CHIP_FAMILY_RS100) ||
              (rinfo->family == CHIP_FAMILY_RS200)) {
             /* This is to workaround the asic bug for RMX, some versions
                of BIOS dosen't have this register initialized correctly.
             */
             OUTREGP(CRTC_MORE_CNTL, CRTC_H_CUTOFF_ACTIVE_EN,
                     ~CRTC_H_CUTOFF_ACTIVE_EN);
          }
        } else {
          tmp = INREG(CONFIG_MEMSIZE);
        }

	/* mem size is bits [28:0], mask off the rest */
	rinfo->video_ram = tmp & CONFIG_MEMSIZE_MASK;

	/* ram type */
	tmp = INREG(MEM_SDRAM_MODE_REG);
	switch ((MEM_CFG_TYPE & tmp) >> 30) {
       	case 0:
       		/* SDR SGRAM (2:1) */
       		strcpy(rinfo->ram_type, "SDR SGRAM");
       		rinfo->ram.ml = 4;
       		rinfo->ram.mb = 4;
       		rinfo->ram.trcd = 1;
       		rinfo->ram.trp = 2;
       		rinfo->ram.twr = 1;
       		rinfo->ram.cl = 2;
       		rinfo->ram.loop_latency = 16;
       		rinfo->ram.rloop = 16;
       		break;
       	case 1:
       		/* DDR SGRAM */
       		strcpy(rinfo->ram_type, "DDR SGRAM");
       		rinfo->ram.ml = 4;
       		rinfo->ram.mb = 4;
       		rinfo->ram.trcd = 3;
       		rinfo->ram.trp = 3;
       		rinfo->ram.twr = 2;
       		rinfo->ram.cl = 3;
       		rinfo->ram.tr2w = 1;
       		rinfo->ram.loop_latency = 16;
       		rinfo->ram.rloop = 16;
		break;
       	default:
       		/* 64-bit SDR SGRAM */
       		strcpy(rinfo->ram_type, "SDR SGRAM 64");
       		rinfo->ram.ml = 4;
       		rinfo->ram.mb = 8;
       		rinfo->ram.trcd = 3;
       		rinfo->ram.trp = 3;
       		rinfo->ram.twr = 1;
       		rinfo->ram.cl = 3;
       		rinfo->ram.tr2w = 1;
       		rinfo->ram.loop_latency = 17;
       		rinfo->ram.rloop = 17;
		break;
	}

	/*
	 * Hack to get around some busted production M6's
	 * reporting no ram
	 */
	if (rinfo->video_ram == 0) {
		switch (pdev->device) {
	       	case PCI_CHIP_RADEON_LY:
		case PCI_CHIP_RADEON_LZ:
	       		rinfo->video_ram = 8192 * 1024;
	       		break;
	       	default:
	       		break;
		}
	}

	RTRACE("radeonfb: probed %s %ldk videoram\n", (rinfo->ram_type), (rinfo->video_ram/1024));

	rinfo->mapped_vram = MAX_MAPPED_VRAM;
	if (rinfo->video_ram < rinfo->mapped_vram)
		rinfo->mapped_vram = rinfo->video_ram;
	for (;;) {
		rinfo->fb_base = (unsigned long) ioremap (rinfo->fb_base_phys,
							  rinfo->mapped_vram);
		if (rinfo->fb_base == 0 && rinfo->mapped_vram > MIN_MAPPED_VRAM) {
			rinfo->mapped_vram /= 2;
			continue;
		}
		memset_io(rinfo->fb_base, 0, rinfo->mapped_vram);
		break;
	}

	if (!rinfo->fb_base) {
		printk (KERN_ERR "radeonfb: cannot map FB\n");
		goto unmap_rom;
	}

	RTRACE("radeonfb: mapped %ldk videoram\n", rinfo->mapped_vram/1024);


	/* Argh. Scary arch !!! */
#ifdef CONFIG_PPC64
	rinfo->fb_base = IO_TOKEN_TO_ADDR(rinfo->fb_base);
#endif

	/*
	 * Check for required workaround for PLL accesses
	 */
	rinfo->R300_cg_workaround = (rinfo->family == CHIP_FAMILY_R300 &&
				     (INREG(CONFIG_CNTL) & CFG_ATI_REV_ID_MASK)
				     == CFG_ATI_REV_A11);

	/*
	 * Map the BIOS ROM if any and retreive PLL parameters from
	 * either BIOS or Open Firmware
	 */
	radeon_map_ROM(rinfo, pdev);

	/*
	 * On x86, the primary display on laptop may have it's BIOS
	 * ROM elsewhere, try to locate it at the legacy memory hole.
	 * We probably need to make sure this is the primary dispay,
	 * but that is difficult without some arch support.
	 */
#ifdef __i386__
	if (rinfo->bios_seg == NULL)
		radeon_find_mem_vbios(rinfo);
#endif /* __i386__ */

	/* Get informations about the board's PLL */
	radeon_get_pllinfo(rinfo);

#ifdef CONFIG_FB_RADEON_I2C
	/* Register I2C bus */
	radeon_create_i2c_busses(rinfo);
#endif

	/* set all the vital stuff */
	radeon_set_fbinfo (rinfo);

	/* Probe screen types */
	radeon_probe_screens(rinfo, monitor_layout, ignore_edid);

	/* Build mode list, check out panel native model */
	radeon_check_modes(rinfo, mode_option);

	/* Register some sysfs stuff (should be done better) */
	if (rinfo->mon1_EDID)
		sysfs_create_bin_file(&rinfo->pdev->dev.kobj, &edid1_attr);
	if (rinfo->mon2_EDID)
		sysfs_create_bin_file(&rinfo->pdev->dev.kobj, &edid2_attr);

	/* save current mode regs before we switch into the new one
	 * so we can restore this upon __exit
	 */
	radeon_save_state (rinfo, &rinfo->init_state);

	pci_set_drvdata(pdev, info);

	/* Enable PM on mobility chips */
	if (rinfo->is_mobility) {
		/* Find PM registers in config space */
		rinfo->pm_reg = pci_find_capability(pdev, PCI_CAP_ID_PM);
		/* Enable dynamic PM of chip clocks */
		radeon_pm_enable_dynamic_mode(rinfo);
		printk("radeonfb: Power Management enabled for Mobility chipsets\n");
	}

	if (register_framebuffer(info) < 0) {
		printk (KERN_ERR "radeonfb: could not register framebuffer\n");
		goto unmap_fb;
	}

#ifdef CONFIG_MTRR
	rinfo->mtrr_hdl = nomtrr ? -1 : mtrr_add(rinfo->fb_base_phys,
						 rinfo->video_ram,
						 MTRR_TYPE_WRCOMB, 1);
#endif

#ifdef CONFIG_PMAC_BACKLIGHT
	if (rinfo->mon1_type == MT_LCD) {
		register_backlight_controller(&radeon_backlight_controller,
					      rinfo, "ati");
		register_backlight_controller(&radeon_backlight_controller,
					      rinfo, "mnca");
	}
#endif

	printk ("radeonfb: %s %s %ld MB\n", rinfo->name, rinfo->ram_type,
		(rinfo->video_ram/(1024*1024)));

	if (rinfo->bios_seg)
		radeon_unmap_ROM(rinfo, pdev);
	RTRACE("radeonfb_pci_register END\n");

	return 0;
unmap_fb:
	iounmap ((void*)rinfo->fb_base);
unmap_rom:	
	if (rinfo->mon1_EDID)
	    kfree(rinfo->mon1_EDID);
	if (rinfo->mon2_EDID)
	    kfree(rinfo->mon2_EDID);
	if (rinfo->mon1_modedb)
		fb_destroy_modedb(rinfo->mon1_modedb);
#ifdef CONFIG_FB_RADEON_I2C
	radeon_delete_i2c_busses(rinfo);
#endif
	if (rinfo->bios_seg)
		radeon_unmap_ROM(rinfo, pdev);
	iounmap ((void*)rinfo->mmio_base);
release_mmio:
	release_mem_region (rinfo->mmio_base_phys,
			    pci_resource_len(pdev, 2));
release_fb:	
	release_mem_region (rinfo->fb_base_phys,
			    pci_resource_len(pdev, 0));
free_rinfo:	
	framebuffer_release(info);
	return -ENODEV;
}



static void __devexit radeonfb_pci_unregister (struct pci_dev *pdev)
{
        struct fb_info *info = pci_get_drvdata(pdev);
        struct radeonfb_info *rinfo = info->par;
 
        if (!rinfo)
                return;
 
	/* restore original state
	 * 
	 * Doesn't quite work yet, possibly because of the PPC hacking
	 * I do on startup, disable for now. --BenH
	 */
        radeon_write_mode (rinfo, &rinfo->init_state);
 
	del_timer_sync(&rinfo->lvds_timer);

#ifdef CONFIG_MTRR
	if (rinfo->mtrr_hdl >= 0)
		mtrr_del(rinfo->mtrr_hdl, 0, 0);
#endif

        unregister_framebuffer(info);

        iounmap ((void*)rinfo->mmio_base);
        iounmap ((void*)rinfo->fb_base);
 
	release_mem_region (rinfo->mmio_base_phys,
			    pci_resource_len(pdev, 2));
	release_mem_region (rinfo->fb_base_phys,
			    pci_resource_len(pdev, 0));

	if (rinfo->mon1_EDID)
		kfree(rinfo->mon1_EDID);
	if (rinfo->mon2_EDID)
		kfree(rinfo->mon2_EDID);
	if (rinfo->mon1_modedb)
		fb_destroy_modedb(rinfo->mon1_modedb);
#ifdef CONFIG_FB_RADEON_I2C
	radeon_delete_i2c_busses(rinfo);
#endif        
        framebuffer_release(info);
}


static struct pci_driver radeonfb_driver = {
	.name		= "radeonfb",
	.id_table	= radeonfb_pci_table,
	.probe		= radeonfb_pci_register,
	.remove		= __devexit_p(radeonfb_pci_unregister),
#ifdef CONFIG_PM
	.suspend       	= radeonfb_pci_suspend,
	.resume		= radeonfb_pci_resume,
#endif /* CONFIG_PM */
};


int __init radeonfb_init (void)
{
	radeonfb_noaccel = noaccel;
	return pci_module_init (&radeonfb_driver);
}


void __exit radeonfb_exit (void)
{
	pci_unregister_driver (&radeonfb_driver);
}

int __init radeonfb_setup (char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep (&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "noaccel", 7)) {
			noaccel = radeonfb_noaccel = 1;
		} else if (!strncmp(this_opt, "mirror", 6)) {
			mirror = 1;
		} else if (!strncmp(this_opt, "force_dfp", 9)) {
			force_dfp = 1;
		} else if (!strncmp(this_opt, "panel_yres:", 11)) {
			panel_yres = simple_strtoul((this_opt+11), NULL, 0);
#ifdef CONFIG_MTRR
		} else if (!strncmp(this_opt, "nomtrr", 6)) {
			nomtrr = 1;
#endif
		} else if (!strncmp(this_opt, "nomodeset", 9)) {
			nomodeset = 1;
		} else if (!strncmp(this_opt, "force_measure_pll", 17)) {
			force_measure_pll = 1;
		} else if (!strncmp(this_opt, "ignore_edid", 11)) {
			ignore_edid = 1;
		} else
			mode_option = this_opt;
	}
	return 0;
}


#ifdef MODULE
module_init(radeonfb_init);
module_exit(radeonfb_exit);
#endif

MODULE_AUTHOR("Ani Joshi");
MODULE_DESCRIPTION("framebuffer driver for ATI Radeon chipset");
MODULE_LICENSE("GPL");
module_param(noaccel, bool, 0);
MODULE_PARM_DESC(noaccel, "bool: disable acceleration");
module_param(nomodeset, bool, 0);
MODULE_PARM_DESC(nomodeset, "bool: disable actual setting of video mode");
module_param(mirror, bool, 0);
MODULE_PARM_DESC(mirror, "bool: mirror the display to both monitors");
module_param(force_dfp, bool, 0);
MODULE_PARM_DESC(force_dfp, "bool: force display to dfp");
module_param(ignore_edid, bool, 0);
MODULE_PARM_DESC(ignore_edid, "bool: Ignore EDID data when doing DDC probe");
module_param(monitor_layout, charp, 0);
MODULE_PARM_DESC(monitor_layout, "Specify monitor mapping (like XFree86)");
module_param(force_measure_pll, bool, 0);
MODULE_PARM_DESC(force_measure_pll, "Force measurement of PLL (debug)");
#ifdef CONFIG_MTRR
module_param(nomtrr, bool, 0);
MODULE_PARM_DESC(nomtrr, "bool: disable use of MTRR registers");
#endif
module_param(panel_yres, int, 0);
MODULE_PARM_DESC(panel_yres, "int: set panel yres");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Specify resolution as \"<xres>x<yres>[-<bpp>][@<refresh>]\" ");
