/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * linux/drivers/ide/serverworks.c		Version 0.3	26 Oct 2001
 *
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Copyright (C) 1998-2000 Michel Aubry
 * Copyright (C) 1998-2000 Andrzej Krzysztofowicz
 * Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
 * Portions copyright (c) 2001 Sun Microsystems
 *
 *
 * RCC/ServerWorks IDE driver for Linux
 *
 *   OSB4: `Open South Bridge' IDE Interface (fn 1)
 *         supports UDMA mode 2 (33 MB/s)
 *
 *   CSB5: `Champion South Bridge' IDE Interface (fn 1)
 *         all revisions support UDMA mode 4 (66 MB/s)
 *         revision A2.0 and up support UDMA mode 5 (100 MB/s)
 *
 *         *** The CSB5 does not provide ANY register ***
 *         *** to detect 80-conductor cable presence. ***
 *
 *
 * here's the default lspci:
 *
 * 00:0f.1 IDE interface: ServerWorks: Unknown device 0211 (prog-if 8a [Master SecP PriP])
 *	Control: I/O+ Mem- BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR+ FastB2B-
 *	Status: Cap- 66Mhz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR-
 *	Latency: 255
 *	Region 4: I/O ports at c200
 * 00: 66 11 11 02 05 01 00 02 00 8a 01 01 00 ff 80 00
 * 10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 20: 01 c2 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 40: 99 99 99 99 ff ff ff ff 0c 0c 00 00 00 00 00 00
 * 50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 * 00:0f.1 IDE interface: ServerWorks: Unknown device 0212 (rev 92) (prog-if 8a [Master SecP PriP])
 *         Subsystem: ServerWorks: Unknown device 0212
 *         Control: I/O+ Mem- BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B-
 *         Status: Cap- 66Mhz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR-
 *         Latency: 64, cache line size 08
 *         Region 0: I/O ports at 01f0
 *         Region 1: I/O ports at 03f4
 *         Region 2: I/O ports at 0170
 *         Region 3: I/O ports at 0374
 *         Region 4: I/O ports at 08b0
 *         Region 5: I/O ports at 1000
 *
 * 00:0f.1 IDE interface: ServerWorks: Unknown device 0212 (rev 92)
 * 00: 66 11 12 02 05 00 00 02 92 8a 01 01 08 40 80 00
 * 10: f1 01 00 00 f5 03 00 00 71 01 00 00 75 03 00 00
 * 20: b1 08 00 00 01 10 00 00 00 00 00 00 66 11 12 02
 * 30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 40: 4f 4f 4f 4f 20 ff ff ff f0 50 44 44 00 00 00 00
 * 50: 00 00 00 00 07 00 44 02 0f 04 03 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 *
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ata-timing.h"
#include "pcihost.h"

#undef DISPLAY_SVWKS_TIMINGS
#undef SVWKS_DEBUG_DRIVE_INFO

static u8 svwks_revision;

#if defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static struct pci_dev *bmide_dev;

static int svwks_get_info(char *, char **, off_t, int);
extern int (*svwks_display_info)(char *, char **, off_t, int); /* ide-proc.c */

static int svwks_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	u32 bibma = pci_resource_start(bmide_dev, 4);
	u32 reg40, reg44;
	u16 reg48, reg56;
	u8  reg54, c0=0, c1=0;

	pci_read_config_dword(bmide_dev, 0x40, &reg40);
	pci_read_config_dword(bmide_dev, 0x44, &reg44);
	pci_read_config_word(bmide_dev, 0x48, &reg48);
	pci_read_config_byte(bmide_dev, 0x54, &reg54);
	pci_read_config_word(bmide_dev, 0x56, &reg56);

        /*
         * at that point bibma+0x2 et bibma+0xa are byte registers
         * to investigate:
         */
	c0 = inb_p((unsigned short)bibma + 0x02);
	c1 = inb_p((unsigned short)bibma + 0x0a);

	switch(bmide_dev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
			p += sprintf(p, "\n                            "
				     "ServerWorks CSB5 Chipset (rev %02x)\n",
				     svwks_revision);
			break;
		case PCI_DEVICE_ID_SERVERWORKS_OSB4IDE:
			p += sprintf(p, "\n                            "
				     "ServerWorks OSB4 Chipset (rev %02x)\n",
				     svwks_revision);
			break;
		default:
			p += sprintf(p, "\n                            "
				     "ServerWorks %04x Chipset (rev %02x)\n",
				     bmide_dev->device, svwks_revision);
			break;
	}

	p += sprintf(p, "------------------------------- General Status ---------------------------------\n");
	p += sprintf(p, "--------------- Primary Channel ---------------- Secondary Channel -------------\n");
	p += sprintf(p, "                %sabled                         %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 -------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s             %s               %s\n",
			(c0&0x20) ? "yes" : "no ",
			(c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ",
			(c1&0x40) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s              %s             %s               %s\n",
			(reg54 & 0x01) ? "yes" : "no ",
			(reg54 & 0x02) ? "yes" : "no ",
			(reg54 & 0x04) ? "yes" : "no ",
			(reg54 & 0x08) ? "yes" : "no " );
	p += sprintf(p, "UDMA enabled:   %s                %s               %s                 %s\n",
			((reg56&0x0005)==0x0005)?"5":
				((reg56&0x0004)==0x0004)?"4":
				((reg56&0x0003)==0x0003)?"3":
				((reg56&0x0002)==0x0002)?"2":
				((reg56&0x0001)==0x0001)?"1":
				((reg56&0x000F))?"?":"0",
			((reg56&0x0050)==0x0050)?"5":
				((reg56&0x0040)==0x0040)?"4":
				((reg56&0x0030)==0x0030)?"3":
				((reg56&0x0020)==0x0020)?"2":
				((reg56&0x0010)==0x0010)?"1":
				((reg56&0x00F0))?"?":"0",
			((reg56&0x0500)==0x0500)?"5":
				((reg56&0x0400)==0x0400)?"4":
				((reg56&0x0300)==0x0300)?"3":
				((reg56&0x0200)==0x0200)?"2":
				((reg56&0x0100)==0x0100)?"1":
				((reg56&0x0F00))?"?":"0",
			((reg56&0x5000)==0x5000)?"5":
				((reg56&0x4000)==0x4000)?"4":
				((reg56&0x3000)==0x3000)?"3":
				((reg56&0x2000)==0x2000)?"2":
				((reg56&0x1000)==0x1000)?"1":
				((reg56&0xF000))?"?":"0");
	p += sprintf(p, "DMA enabled:    %s                %s               %s                 %s\n",
			((reg44&0x00002000)==0x00002000)?"2":
				((reg44&0x00002100)==0x00002100)?"1":
				((reg44&0x00007700)==0x00007700)?"0":
				((reg44&0x0000FF00)==0x0000FF00)?"X":"?",
			((reg44&0x00000020)==0x00000020)?"2":
				((reg44&0x00000021)==0x00000021)?"1":
				((reg44&0x00000077)==0x00000077)?"0":
				((reg44&0x000000FF)==0x000000FF)?"X":"?",
			((reg44&0x20000000)==0x20000000)?"2":
				((reg44&0x21000000)==0x21000000)?"1":
				((reg44&0x77000000)==0x77000000)?"0":
				((reg44&0xFF000000)==0xFF000000)?"X":"?",
			((reg44&0x00200000)==0x00200000)?"2":
				((reg44&0x00210000)==0x00210000)?"1":
				((reg44&0x00770000)==0x00770000)?"0":
				((reg44&0x00FF0000)==0x00FF0000)?"X":"?");

		p += sprintf(p, "PIO  enabled:   %s                %s               %s                 %s\n",
			((reg40&0x00002000)==0x00002000)?"4":
				((reg40&0x00002200)==0x00002200)?"3":
				((reg40&0x00003400)==0x00003400)?"2":
				((reg40&0x00004700)==0x00004700)?"1":
				((reg40&0x00005D00)==0x00005D00)?"0":"?",
			((reg40&0x00000020)==0x00000020)?"4":
				((reg40&0x00000022)==0x00000022)?"3":
				((reg40&0x00000034)==0x00000034)?"2":
				((reg40&0x00000047)==0x00000047)?"1":
				((reg40&0x0000005D)==0x0000005D)?"0":"?",
			((reg40&0x20000000)==0x20000000)?"4":
				((reg40&0x22000000)==0x22000000)?"3":
				((reg40&0x34000000)==0x34000000)?"2":
				((reg40&0x47000000)==0x47000000)?"1":
				((reg40&0x5D000000)==0x5D000000)?"0":"?",
			((reg40&0x00200000)==0x00200000)?"4":
				((reg40&0x00220000)==0x00220000)?"3":
				((reg40&0x00340000)==0x00340000)?"2":
				((reg40&0x00470000)==0x00470000)?"1":
				((reg40&0x005D0000)==0x005D0000)?"0":"?");
	return p-buffer;	 /* => must be less than 4k! */
}

static byte svwks_proc;

#endif  /* defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS) */

#define SVWKS_CSB5_REVISION_NEW	0x92 /* min PCI_REVISION_ID for UDMA5 (A2.0) */

static struct pci_dev *isa_dev;

static int svwks_ratemask(struct ata_device *drive)
{
	struct pci_dev *dev = drive->channel->pci_dev;
	int map = 0;

	if (!eighty_ninty_three(drive))
		return XFER_UDMA;

	switch(dev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_CSB5IDE:
			if (svwks_revision >= SVWKS_CSB5_REVISION_NEW)
				map |= XFER_UDMA_100;
			map |= XFER_UDMA_66;
		case PCI_DEVICE_ID_SERVERWORKS_OSB4IDE:
			map |= XFER_UDMA;
			break;
	}
	return map;
}

static int svwks_tune_chipset(struct ata_device *drive, byte speed)
{
	static u8 dma_modes[]	= { 0x77, 0x21, 0x20 };
	static u8 pio_modes[]	= { 0x5d, 0x47, 0x34, 0x22, 0x20 };

	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev	= hwif->pci_dev;
	byte unit		= (drive->select.b.unit & 0x01);
	byte csb5		= (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) ? 1 : 0;

	byte drive_pci, drive_pci2;
	byte drive_pci3	= hwif->unit ? 0x57 : 0x56;

	byte ultra_enable, ultra_timing, dma_timing, pio_timing;
	unsigned short csb5_pio;

	byte pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;

        switch (drive->dn) {
		case 0: drive_pci = 0x41; break;
		case 1: drive_pci = 0x40; break;
		case 2: drive_pci = 0x43; break;
		case 3: drive_pci = 0x42; break;
		default:
			return -1;
	}
	drive_pci2 = drive_pci + 4;

	pci_read_config_byte(dev, drive_pci, &pio_timing);
	pci_read_config_byte(dev, drive_pci2, &dma_timing);
	pci_read_config_byte(dev, drive_pci3, &ultra_timing);
	pci_read_config_word(dev, 0x4A, &csb5_pio);
	pci_read_config_byte(dev, 0x54, &ultra_enable);

#ifdef DEBUG
	printk("%s: UDMA 0x%02x DMAPIO 0x%02x PIO 0x%02x ",
		drive->name, ultra_timing, dma_timing, pio_timing);
#endif

	pio_timing	&= ~0xFF;
	dma_timing	&= ~0xFF;
	ultra_timing	&= ~(0x0F << (4*unit));
	ultra_enable	&= ~(0x01 << drive->dn);
	csb5_pio	&= ~(0x0F << (4*drive->dn));

	switch(speed) {
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			pio_timing |= pio_modes[speed - XFER_PIO_0];
			csb5_pio   |= ((speed - XFER_PIO_0) << (4*drive->dn));
			break;

#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			pio_timing |= pio_modes[pio];
			csb5_pio   |= (pio << (4*drive->dn));
			dma_timing |= dma_modes[speed - XFER_MW_DMA_0];
			break;

		case XFER_UDMA_5:
		case XFER_UDMA_4:
		case XFER_UDMA_3:
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			pio_timing   |= pio_modes[pio];
			csb5_pio     |= (pio << (4*drive->dn));
			dma_timing   |= dma_modes[2];
			ultra_timing |= ((speed - XFER_UDMA_0) << (4*unit));
			ultra_enable |= (0x01 << drive->dn);
#endif
		default:
			break;
	}

#ifdef DEBUG
	printk("%s: UDMA 0x%02x DMAPIO 0x%02x PIO 0x%02x ",
		drive->name, ultra_timing, dma_timing, pio_timing);
#endif

#if SVWKS_DEBUG_DRIVE_INFO
	printk("%s: %02x drive%d\n", drive->name, speed, drive->dn);
#endif

	pci_write_config_byte(dev, drive_pci, pio_timing);
	if (csb5)
		pci_write_config_word(dev, 0x4A, csb5_pio);

#ifdef CONFIG_BLK_DEV_IDEDMA
	pci_write_config_byte(dev, drive_pci2, dma_timing);
	pci_write_config_byte(dev, drive_pci3, ultra_timing);
	pci_write_config_byte(dev, 0x54, ultra_enable);
#endif
	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;

	return ide_config_drive_speed(drive, speed);
}

static void config_chipset_for_pio(struct ata_device *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio = drive->id->eide_pio_modes;
	byte timing, speed, pio;

	pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;

	if (xfer_pio > 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0)
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	else
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;

	timing = (xfer_pio >= pio) ? xfer_pio : pio;

	switch(timing) {
		case 4: speed = XFER_PIO_4;break;
		case 3: speed = XFER_PIO_3;break;
		case 2: speed = XFER_PIO_2;break;
		case 1: speed = XFER_PIO_1;break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
	}
	(void) svwks_tune_chipset(drive, speed);
	drive->current_speed = speed;
}

static void svwks_tune_drive(struct ata_device *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4;break;
		case 3:		speed = XFER_PIO_3;break;
		case 2:		speed = XFER_PIO_2;break;
		case 1:		speed = XFER_PIO_1;break;
		default:	speed = XFER_PIO_0;break;
	}
	(void) svwks_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma(struct ata_device *drive)
{
	int map;
	byte mode;

	/* FIXME: check SWDMA modes --bkz */
	map = XFER_MWDMA | svwks_ratemask(drive);
	mode = ata_timing_mode(drive, map);

	return !svwks_tune_chipset(drive, mode);
}

static int config_drive_xfer_rate(struct ata_device *drive)
{
	struct hd_driveid *id = drive->id;
	int on = 1;
	int verbose = 1;

	if (id && (id->capability & 1) && drive->channel->autodma) {
		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			on = 0;
			goto fast_ata_pio;
		}
		on = 0;
		verbose = 0;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x003F) {
				/* Force if Capable UltraDMA */
				on = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (!on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x007)) {
				/* Force if Capable regular DMA modes */
				on = config_chipset_for_dma(drive);
				if (!on)
					goto no_dma_set;
			}
		} else if (udma_white_list(drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			on = config_chipset_for_dma(drive);
			if (!on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		on = 0;
		verbose = 0;
no_dma_set:
		config_chipset_for_pio(drive);
	}

	udma_enable(drive, on, verbose);

	return 0;
}

static int svwks_udma_stop(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long dma_base = ch->dma_base;
	u8 dma_stat;

	if(inb(dma_base+0x02)&1)
	{
#if 0
		int i;
		printk(KERN_ERR "Curious - OSB4 thinks the DMA is still running.\n");
		for(i=0;i<10;i++)
		{
			if(!(inb(dma_base+0x02)&1))
			{
				printk(KERN_ERR "OSB4 now finished.\n");
				break;
			}
			udelay(5);
		}
#endif
		printk(KERN_CRIT "Serverworks OSB4 in impossible state.\n");
		printk(KERN_CRIT "Disable UDMA or if you are using Seagate then try switching disk types\n");
		printk(KERN_CRIT "on this controller. Please report this event to osb4-bug@ide.cabal.tm\n");
#if 0
		/* Panic might sys_sync -> death by corrupt disk */
		panic("OSB4: continuing might cause disk corruption.\n");
#else
		printk(KERN_CRIT "OSB4: continuing might cause disk corruption.\n");
		while(1)
			cpu_relax();
#endif
	}

	drive->waiting_for_dma = 0;
	outb(inb(dma_base)&~1, dma_base);	/* stop DMA */
	dma_stat = inb(dma_base+2);		/* get DMA status */
	outb(dma_stat|6, dma_base+2);		/* clear the INTR & ERROR bits */
	udma_destroy_table(ch);			/* purge DMA mappings */

	return (dma_stat & 7) != 4 ? (0x10 | dma_stat) : 0;	/* verify good DMA status */
}

static int svwks_dmaproc(struct ata_device *drive)
{
	return config_drive_xfer_rate(drive);
}
#endif

static unsigned int __init svwks_init_chipset(struct pci_dev *dev)
{
	unsigned int reg;
	byte btr;

	/* save revision id to determine DMA capability */
	pci_read_config_byte(dev, PCI_REVISION_ID, &svwks_revision);

	/* force Master Latency Timer value to 64 PCICLKs */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x40);

	/* OSB4 : South Bridge and IDE */
	if (dev->device == PCI_DEVICE_ID_SERVERWORKS_OSB4IDE) {
		isa_dev = pci_find_device(PCI_VENDOR_ID_SERVERWORKS,
			  PCI_DEVICE_ID_SERVERWORKS_OSB4, NULL);
		if (isa_dev) {
			pci_read_config_dword(isa_dev, 0x64, &reg);
			reg &= ~0x00002000; /* disable 600ns interrupt mask */
			reg |=  0x00004000; /* enable UDMA/33 support */
			pci_write_config_dword(isa_dev, 0x64, reg);
		}
	}

	/* setup CSB5 : South Bridge and IDE */
	else if (dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE) {
		/* setup the UDMA Control register
		 *
		 * 1. clear bit 6 to enable DMA
		 * 2. enable DMA modes with bits 0-1
		 *      00 : legacy
		 *      01 : udma2
		 *      10 : udma2/udma4
		 *      11 : udma2/udma4/udma5
		 */
		pci_read_config_byte(dev, 0x5A, &btr);
		btr &= ~0x40;
		btr |= (svwks_revision >= SVWKS_CSB5_REVISION_NEW) ? 0x3 : 0x2;
		pci_write_config_byte(dev, 0x5A, btr);
	}

#if defined(DISPLAY_SVWKS_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!svwks_proc) {
		svwks_proc = 1;
		bmide_dev = dev;
		svwks_display_info = &svwks_get_info;
	}
#endif /* DISPLAY_SVWKS_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

/* On Dell PowerEdge servers with a CSB5, the top two bits of the subsystem
 * device ID indicate presence of an 80-pin cable.
 * Bit 15 clear = secondary IDE channel does not have 80-pin cable.
 * Bit 15 set   = secondary IDE channel has 80-pin cable.
 * Bit 14 clear = primary IDE channel does not have 80-pin cable.
 * Bit 14 set   = primary IDE channel has 80-pin cable.
 */
static unsigned int __init ata66_svwks_dell(struct ata_channel *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		return ((1 << (hwif->unit + 14)) &
			dev->subsystem_device) ? 1 : 0;
	return 0;
}

/* Sun Cobalt Alpine hardware avoids the 80-pin cable
 * detect issue by attaching the drives directly to the board.
 * This check follows the Dell precedent (how scary is that?!)
 *
 * WARNING: this only works on Alpine hardware!
 */
static unsigned int __init ata66_svwks_cobalt(struct ata_channel *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN &&
	    dev->vendor	== PCI_VENDOR_ID_SERVERWORKS &&
	    dev->device == PCI_DEVICE_ID_SERVERWORKS_CSB5IDE)
		return ((1 << (hwif->unit + 14)) &
			dev->subsystem_device) ? 1 : 0;
	return 0;
}

static unsigned int __init svwks_ata66_check(struct ata_channel *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

	/* Dell PowerEdge */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_DELL)
		return ata66_svwks_dell (hwif);

	/* Cobalt Alpine */
	if (dev->subsystem_vendor == PCI_VENDOR_ID_SUN)
		return ata66_svwks_cobalt (hwif);

	return 0;
}

static void __init ide_init_svwks(struct ata_channel *hwif)
{
	if (!hwif->irq)
		hwif->irq = hwif->unit ? 15 : 14;

	hwif->tuneproc = &svwks_tune_drive;
	hwif->speedproc = &svwks_tune_chipset;

#ifndef CONFIG_BLK_DEV_IDEDMA
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
#else
	if (hwif->dma_base) {
#ifdef CONFIG_IDEDMA_AUTO
		if (!noautodma)
			hwif->autodma = 1;
#endif
		hwif->udma_stop = svwks_udma_stop;
		hwif->XXX_udma = svwks_dmaproc;
		hwif->highmem = 1;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}
#endif
}


/* module data table */
static struct ata_pci_device chipsets[] __initdata = {
        {
		vendor: PCI_VENDOR_ID_SERVERWORKS,
		device: PCI_DEVICE_ID_SERVERWORKS_OSB4IDE,
		init_chipset: svwks_init_chipset,
		ata66_check: svwks_ata66_check,
		init_channel: ide_init_svwks,
		bootable: ON_BOARD,
		flags: ATA_F_DMA
	},
	{
		vendor: PCI_VENDOR_ID_SERVERWORKS,
		device: PCI_DEVICE_ID_SERVERWORKS_CSB5IDE,
		init_chipset: svwks_init_chipset,
		ata66_check: svwks_ata66_check,
		init_channel: ide_init_svwks,
		bootable: ON_BOARD
	},
};

int __init init_svwks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chipsets); ++i) {
		ata_register_chipset(&chipsets[i]);
	}

        return 0;
}
