/* $Id: psycho.c,v 1.22 1997/08/31 03:51:40 davem Exp $
 * psycho.c: Ultra/AX U2P PCI controller support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caipfs.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/ebus.h>
#include <asm/sbus.h> /* for sanity check... */

#ifndef CONFIG_PCI

int pcibios_present(void)
{
	return 0;
}

asmlinkage int sys_pciconfig_read(unsigned long bus,
				  unsigned long dfn,
				  unsigned long off,
				  unsigned long len,
				  unsigned char *buf)
{
	return 0;
}

asmlinkage int sys_pciconfig_write(unsigned long bus,
				   unsigned long dfn,
				   unsigned long off,
				   unsigned long len,
				   unsigned char *buf)
{
	return 0;
}

#else

#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/oplib.h>
#include <asm/pbm.h>
#include <asm/uaccess.h>

struct linux_psycho *psycho_root = NULL;

/* This is used to make the scan_bus in the generic PCI code be
 * a nop, as we need to control the actual bus probing sequence.
 * After that we leave it on of course.
 */
static int pci_probe_enable = 0;

static inline unsigned long long_align(unsigned long addr)
{
	return ((addr + (sizeof(unsigned long) - 1)) &
		~(sizeof(unsigned long) - 1));
}

static unsigned long psycho_iommu_init(struct linux_psycho *psycho,
				       unsigned long memory_start)
{
	unsigned long tsbbase = PAGE_ALIGN(memory_start);
	unsigned long control, i;
	unsigned long *iopte;

	memory_start = (tsbbase + ((32 * 1024) * 8));
	iopte = (unsigned long *)tsbbase;

	for(i = 0; i < (65536 / 2); i++) {
		*iopte = (IOPTE_VALID | IOPTE_64K |
			  IOPTE_CACHE | IOPTE_WRITE);
		*iopte |= (i << 16);
		iopte++;
	}

	psycho->psycho_regs->iommu_tsbbase = __pa(tsbbase);

	control = psycho->psycho_regs->iommu_control;
	control &= ~(IOMMU_CTRL_TSBSZ);
	control |= (IOMMU_TSBSZ_32K | IOMMU_CTRL_TBWSZ | IOMMU_CTRL_ENAB);
	psycho->psycho_regs->iommu_control = control;

	return memory_start;
}

extern void prom_pbm_ranges_init(int node, struct linux_pbm_info *pbm);

unsigned long pcibios_init(unsigned long memory_start, unsigned long memory_end)
{
	struct linux_prom64_registers pr_regs[3];
	char namebuf[128];
	u32 portid;
	int node;

	printk("PSYCHO: Probing for controllers.\n");

	memory_start = long_align(memory_start);
	node = prom_getchild(prom_root_node);
	while((node = prom_searchsiblings(node, "pci")) != 0) {
		struct linux_psycho *psycho = (struct linux_psycho *)memory_start;
		struct linux_psycho *search;
		struct linux_pbm_info *pbm = NULL;
		u32 busrange[2];
		int err, is_pbm_a;

		portid = prom_getintdefault(node, "upa-portid", 0xff);
		for(search = psycho_root; search; search = search->next) {
			if(search->upa_portid == portid) {
				psycho = search;

				/* This represents _this_ instance, so it's
				 * which ever one does _not_ have the prom node
				 * info filled in yet.
				 */
				is_pbm_a = (psycho->pbm_A.prom_node == 0);
				goto other_pbm;
			}
		}

		memory_start = long_align(memory_start + sizeof(struct linux_psycho));

		memset(psycho, 0, sizeof(*psycho));

		psycho->next = psycho_root;
		psycho_root = psycho;

		psycho->upa_portid = portid;

		/* Map in PSYCHO register set and report the presence of this PSYCHO. */
		err = prom_getproperty(node, "reg",
				       (char *)&pr_regs[0], sizeof(pr_regs));
		if(err == 0 || err == -1) {
			prom_printf("PSYCHO: Error, cannot get U2P registers "
				    "from PROM.\n");
			prom_halt();
		}

		/* Third REG in property is base of entire PSYCHO register space. */
		psycho->psycho_regs = sparc_alloc_io((pr_regs[2].phys_addr & 0xffffffff),
						     NULL, sizeof(struct psycho_regs),
						     "PSYCHO Registers",
						     (pr_regs[2].phys_addr >> 32), 0);
		if(psycho->psycho_regs == NULL) {
			prom_printf("PSYCHO: Error, cannot map PSYCHO "
				    "main registers.\n");
			prom_halt();
		}

		printk("PSYCHO: Found controller, main regs at %p\n",
		       psycho->psycho_regs);
#if 0
		printk("PSYCHO: Interrupt retry [%016lx]\n",
		       psycho->psycho_regs->irq_retry);
#endif
		psycho->psycho_regs->irq_retry = 0xff;

		/* Now map in PCI config space for entire PSYCHO. */
		psycho->pci_config_space =
			sparc_alloc_io(((pr_regs[2].phys_addr & 0xffffffff)+0x01000000),
				       NULL, 0x01000000,
				       "PCI Config Space",
				       (pr_regs[2].phys_addr >> 32), 0);
		if(psycho->pci_config_space == NULL) {
			prom_printf("PSYCHO: Error, cannot map PCI config space.\n");
			prom_halt();
		}

		/* Report some more info. */
		printk("PSYCHO: PCI config space at %p\n", psycho->pci_config_space);

		memory_start = psycho_iommu_init(psycho, memory_start);

		is_pbm_a = ((pr_regs[0].phys_addr & 0x6000) == 0x2000);

		/* Enable arbitration for all PCI slots. */
		psycho->psycho_regs->pci_a_control |= 0x3f;
		psycho->psycho_regs->pci_b_control |= 0x3f;

	other_pbm:
		if(is_pbm_a)
			pbm = &psycho->pbm_A;
		else
			pbm = &psycho->pbm_B;

		pbm->parent = psycho;
		pbm->IO_assignments = NULL;
		pbm->MEM_assignments = NULL;
		pbm->prom_node = node;

		prom_getstring(node, "name", namebuf, sizeof(namebuf));
		strcpy(pbm->prom_name, namebuf);

		/* Now the ranges. */
		prom_pbm_ranges_init(node, pbm);

		/* Finally grab the pci bus root array for this pbm after
		 * having found the bus range existing under it.
		 */
		err = prom_getproperty(node, "bus-range",
				       (char *)&busrange[0], sizeof(busrange));
		if(err == 0 || err == -1) {
			prom_printf("PSYCHO: Error, cannot get PCI bus range.\n");
			prom_halt();
		}
		pbm->pci_first_busno = busrange[0];
		pbm->pci_last_busno = busrange[1];
		memset(&pbm->pci_bus, 0, sizeof(struct pci_bus));

		node = prom_getsibling(node);
		if(!node)
			break;
	}

	/* Last minute sanity check. */
	if(psycho_root == NULL && SBus_chain == NULL) {
		prom_printf("Fatal error, neither SBUS nor PCI bus found.\n");
		prom_halt();
	}

	return memory_start;
}

int pcibios_present(void)
{
	return psycho_root != NULL;
}

int pcibios_find_device (unsigned short vendor, unsigned short device_id,
			 unsigned short index, unsigned char *bus,
			 unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->vendor == vendor && dev->device == device_id) {
			if (curr == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

int pcibios_find_class (unsigned int class_code, unsigned short index,
			unsigned char *bus, unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->class == class_code) {
			if (curr == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static inline struct pci_vma *pci_find_vma(struct linux_pbm_info *pbm,
					   unsigned long start,
					   int io)
{
	struct pci_vma *vp = (io ? pbm->IO_assignments : pbm->MEM_assignments);

	while(vp) {
		if(vp->end > start)
			break;
		vp = vp->next;
	}
	return vp;
}

static inline void pci_add_vma(struct linux_pbm_info *pbm, struct pci_vma *new, int io)
{
	struct pci_vma *vp = (io ? pbm->IO_assignments : pbm->MEM_assignments);

	if(!vp) {
		new->next = NULL;
		if(io)
			pbm->IO_assignments = new;
		else
			pbm->MEM_assignments = new;
	} else {
		struct pci_vma *prev = NULL;

		while(vp && (vp->end < new->end)) {
			prev = vp;
			vp = vp->next;
		}
		new->next = vp;
		if(!prev) {
			if(io)
				pbm->IO_assignments = new;
			else
				pbm->MEM_assignments = new;
		} else {
			prev->next = new;
		}

		/* Check for programming errors. */
		if(vp &&
		   ((vp->start >= new->start && vp->start < new->end) ||
		    ((vp->end - 1) >= new->start && (vp->end - 1) < new->end))) {
			prom_printf("pci_add_vma: Wheee, overlapping %s PCI vma's\n",
				    io ? "IO" : "MEM");
			prom_printf("pci_add_vma: vp[%016lx:%016lx] "
				    "new[%016lx:%016lx]\n",
				    vp->start, vp->end,
				    new->start, new->end);
		}
	}
}

static unsigned long *pci_alloc_arena = NULL;

static inline void pci_init_alloc_init(unsigned long *mstart)
{
	pci_alloc_arena = mstart;
}

static inline void pci_init_alloc_fini(void)
{
	pci_alloc_arena = NULL;
}

static void *pci_init_alloc(int size)
{
	unsigned long start = long_align(*pci_alloc_arena);
	void *mp = (void *)start;

	if(!pci_alloc_arena) {
		prom_printf("pci_init_alloc: pci_vma arena not init'd\n");
		prom_halt();
	}
	start += size;
	*pci_alloc_arena = start;
	return mp;
}

static inline struct pci_vma *pci_vma_alloc(void)
{
	return pci_init_alloc(sizeof(struct pci_vma));
}

static inline struct pcidev_cookie *pci_devcookie_alloc(void)
{
	return pci_init_alloc(sizeof(struct pcidev_cookie));
}

static void pbm_probe(struct linux_pbm_info *pbm, unsigned long *mstart)
{
	struct pci_bus *pbus = &pbm->pci_bus;

	/* PSYCHO PBM's include child PCI bridges in bus-range property,
	 * but we don't scan each of those ourselves, Linux generic PCI
	 * probing code will find child bridges and link them into this
	 * pbm's root PCI device hierarchy.
	 */
	pbus->number = pbm->pci_first_busno;
	pbus->sysdata = pbm;
	pbus->subordinate = pci_scan_bus(pbus, mstart);
}

static int pdev_to_pnode_sibtraverse(struct linux_pbm_info *pbm,
				     struct pci_dev *pdev,
				     int node)
{
	struct linux_prom_pci_registers pregs[PROMREG_MAX];
	int err;

	while(node) {
		int child;

		child = prom_getchild(node);
		if(child != 0 && child != -1) {
			int res;

			res = pdev_to_pnode_sibtraverse(pbm, pdev, child);
			if(res != 0 && res != -1)
				return res;
		}
		err = prom_getproperty(node, "reg", (char *)&pregs[0], sizeof(pregs));
		if(err != 0 && err != -1) {
			u32 devfn = (pregs[0].phys_hi >> 8) & 0xff;

			if(devfn == pdev->devfn)
				return node; /* Match */
		}

		node = prom_getsibling(node);
	}
	return 0;
}

static void pdev_cookie_fillin(struct linux_pbm_info *pbm, struct pci_dev *pdev)
{
	struct pcidev_cookie *pcp;
	int node = prom_getchild(pbm->prom_node);

	node = pdev_to_pnode_sibtraverse(pbm, pdev, node);
	if(node == 0)
		node = -1;
	pcp = pci_devcookie_alloc();
	pcp->pbm = pbm;
	pcp->prom_node = node;
	pdev->sysdata = pcp;
}

static void fill_in_pbm_cookies(struct linux_pbm_info *pbm)
{
	struct pci_bus *pbtmp, *pbus = &pbm->pci_bus;
	struct pci_dev *pdev;

	for(pbtmp = pbus->children; pbtmp; pbtmp = pbtmp->children)
		pbtmp->sysdata = pbm;

	for( ; pbus; pbus = pbus->children)
		for(pdev = pbus->devices; pdev; pdev = pdev->sibling)
			pdev_cookie_fillin(pbm, pdev);
}

/* #define RECORD_ASSIGNMENTS_DEBUG */

/* Walk PROM device tree under PBM, looking for 'assigned-address'
 * properties, and recording them in pci_vma's linked in via
 * PBM->assignments.
 */
static int gimme_ebus_assignments(int node, struct linux_prom_pci_registers *aregs)
{
	struct linux_prom_ebus_ranges erng[PROMREG_MAX];
	int err, iter;

	err = prom_getproperty(node, "ranges", (char *)&erng[0], sizeof(erng));
	if(err == 0 || err == -1) {
		prom_printf("EBUS: fatal error, no range property.\n");
		prom_halt();
	}
	err = (err / sizeof(struct linux_prom_ebus_ranges));
	for(iter = 0; iter < err; iter++) {
		struct linux_prom_ebus_ranges *ep = &erng[iter];
		struct linux_prom_pci_registers *ap = &aregs[iter];

		ap->phys_hi = ep->parent_phys_hi;
		ap->phys_mid = ep->parent_phys_mid;
		ap->phys_lo = ep->parent_phys_lo;
	}
	return err;
}

static void assignment_process(struct linux_pbm_info *pbm, int node)
{
	struct linux_prom_pci_registers aregs[PROMREG_MAX];
	char pname[256];
	int err, iter, numa;

	err = prom_getproperty(node, "name", (char *)&pname[0], sizeof(pname));
	if(strncmp(pname, "ebus", 4) == 0) {
		numa = gimme_ebus_assignments(node, &aregs[0]);
	} else {
		err = prom_getproperty(node, "assigned-addresses",
				       (char *)&aregs[0], sizeof(aregs));

		/* No assignments, nothing to do. */
		if(err == 0 || err == -1)
			return;

		numa = (err / sizeof(struct linux_prom_pci_ranges));
	}

	for(iter = 0; iter < numa; iter++) {
		struct linux_prom_pci_registers *ap = &aregs[iter];
		struct pci_vma *vp;
		int space, breg, io;

		space = (ap->phys_hi >> 24) & 3;
		if(space != 1 && space != 2)
			continue;
		io = (space == 1);

		breg = (ap->phys_hi & 0xff);
		if(breg == PCI_ROM_ADDRESS)
			continue;

		vp = pci_vma_alloc();

		/* XXX Means we don't support > 32-bit range of
		 * XXX PCI MEM space, PSYCHO/PBM does not support it
		 * XXX either due to it's layout so...
		 */
		vp->start = ap->phys_lo;
		vp->end = vp->start + ap->size_lo;
		vp->base_reg = breg;

		/* Sanity */
		if(io && (vp->end & ~(0xffff))) {
			prom_printf("assignment_process: Out of range PCI I/O "
				    "[%08lx:%08lx]\n", vp->start, vp->end);
			prom_halt();
		}

		pci_add_vma(pbm, vp, io);
	}
}

static void assignment_walk_siblings(struct linux_pbm_info *pbm, int node)
{
	while(node) {
		int child = prom_getchild(node);
		if(child)
			assignment_walk_siblings(pbm, child);

		assignment_process(pbm, node);

		node = prom_getsibling(node);
	}
}

static void record_assignments(struct linux_pbm_info *pbm)
{
	assignment_walk_siblings(pbm, prom_getchild(pbm->prom_node));
}

/* #define FIXUP_REGS_DEBUG */

static void fixup_regs(struct pci_dev *pdev,
		       struct linux_pbm_info *pbm,
		       struct linux_prom_pci_registers *pregs,
		       int nregs,
		       struct linux_prom_pci_registers *assigned,
		       int numaa)
{
	int preg, rng;
	int IO_seen = 0;
	int MEM_seen = 0;

	for(preg = 0; preg < nregs; preg++) {
		struct linux_prom_pci_registers *ap = NULL;
		int bustype = (pregs[preg].phys_hi >> 24) & 0x3;
		int bsreg, brindex;
		u64 pci_addr;

		if(bustype == 0) {
			/* Config space cookie, nothing to do. */
			if(preg != 0)
				prom_printf("fixup_doit: strange, config space not 0\n");
			continue;
		} else if(bustype == 3) {
			/* XXX add support for this... */
			prom_printf("fixup_doit: Warning, ignoring 64-bit PCI "
				    "memory space, tell DaveM.\n");
			continue;
		}
		bsreg = (pregs[preg].phys_hi & 0xff);

		/* We can safely ignore these. */
		if(bsreg == PCI_ROM_ADDRESS)
			continue;

		/* Sanity */
		if((bsreg < PCI_BASE_ADDRESS_0) ||
		   (bsreg > (PCI_BASE_ADDRESS_5 + 4)) ||
		   (bsreg & 3)) {
			prom_printf("fixup_doit: Warning, ignoring bogus basereg [%x]\n",
				    bsreg);
			continue;
		}

		brindex = (bsreg - PCI_BASE_ADDRESS_0) >> 2;
		if(numaa) {
			int r;

			for(r = 0; r < numaa; r++) {
				int abreg;

				abreg = (assigned[r].phys_hi & 0xff);
				if(abreg == bsreg) {
					ap = &assigned[r];
					break;
				}
			}
		}

		/* Now construct UPA physical address. */
		pci_addr  = (((u64)pregs[preg].phys_mid) << 32UL);
		pci_addr |= (((u64)pregs[preg].phys_lo));

		if(ap) {
			pci_addr += ((u64)ap->phys_lo);
			pci_addr += (((u64)ap->phys_mid) << 32UL);
		}

		/* Final step, apply PBM range. */
		for(rng = 0; rng < pbm->num_pbm_ranges; rng++) {
			struct linux_prom_pci_ranges *rp = &pbm->pbm_ranges[rng];
			int space = (rp->child_phys_hi >> 24) & 3;

			if(space == bustype) {
				pci_addr += ((u64)rp->parent_phys_lo);
				pci_addr += (((u64)rp->parent_phys_hi) << 32UL);
				break;
			}
		}
		if(rng == pbm->num_pbm_ranges) {
			/* AIEEE */
			prom_printf("fixup_doit: YIEEE, cannot find PBM ranges\n");
		}
		pdev->base_address[brindex] = (unsigned long)__va(pci_addr);

		/* Preserve I/O space bit. */
		if(bustype == 0x1) {
			pdev->base_address[brindex] |= 1;
			IO_seen = 1;
		} else {
			MEM_seen = 1;
		}
	}

	/* Now handle assignments PROM did not take care of. */
	if(nregs) {
		int breg;

		for(breg = PCI_BASE_ADDRESS_0; breg <= PCI_BASE_ADDRESS_5; breg += 4) {
			unsigned int rtmp, ridx = ((breg - PCI_BASE_ADDRESS_0) >> 2);
			unsigned int base = (unsigned int)pdev->base_address[ridx];
			struct pci_vma *vp;
			u64 pci_addr;
			int io;

			if(pdev->base_address[ridx] > PAGE_OFFSET)
				continue;

			io = (base & PCI_BASE_ADDRESS_SPACE)==PCI_BASE_ADDRESS_SPACE_IO;
			base &= ~((io ?
				   PCI_BASE_ADDRESS_IO_MASK :
				   PCI_BASE_ADDRESS_MEM_MASK));
			vp = pci_find_vma(pbm, base, io);
			if(!vp || vp->start > base) {
				unsigned int size, new_base;

				pcibios_read_config_dword(pdev->bus->number,
							  pdev->devfn,
							  breg, &rtmp);
				pcibios_write_config_dword(pdev->bus->number,
							   pdev->devfn,
							   breg, 0xffffffff);
				pcibios_read_config_dword(pdev->bus->number,
							  pdev->devfn,
							  breg, &size);
				if(io)
					size &= ~1;
				size = (~(size) + 1);
				if(!size)
					continue;

				new_base = 0;
				for(vp=pci_find_vma(pbm,new_base,io); ; vp=vp->next) {
					if(!vp || new_base + size <= vp->start)
						break;
					new_base = (vp->end + (size - 1)) & ~(size-1);
				}
				if(vp && (new_base + size > vp->start)) {
					prom_printf("PCI: Impossible full %s space.\n",
						    (io ? "IO" : "MEM"));
					prom_halt();
				}
				vp = pci_vma_alloc();
				vp->start = new_base;
				vp->end = vp->start + size;
				vp->base_reg = breg;

				/* Sanity */
				if(io && vp->end & ~(0xffff)) {
					prom_printf("PCI: Out of range PCI I/O "
						    "[%08lx:%08lx] during fixup\n",
						    vp->start, vp->end);
					prom_halt();
				}
				pci_add_vma(pbm, vp, io);

				rtmp = new_base;
				if(io)
					rtmp |= (rtmp & PCI_BASE_ADDRESS_IO_MASK);
				else
					rtmp |= (rtmp & PCI_BASE_ADDRESS_MEM_MASK);
				pcibios_write_config_dword(pdev->bus->number,
							   pdev->devfn,
							   breg, rtmp);

				/* Apply PBM ranges and update pci_dev. */
				pci_addr = new_base;
				for(rng = 0; rng < pbm->num_pbm_ranges; rng++) {
					struct linux_prom_pci_ranges *rp;
					int rspace;

					rp = &pbm->pbm_ranges[rng];
					rspace = (rp->child_phys_hi >> 24) & 3;
					if(io && rspace != 1)
						continue;
					else if(!io && rspace != 2)
						continue;
					pci_addr += ((u64)rp->parent_phys_lo);
					pci_addr += (((u64)rp->parent_phys_hi)<<32UL);
					break;
				}
				if(rng == pbm->num_pbm_ranges) {
					/* AIEEE */
					prom_printf("fixup_doit: YIEEE, cannot find "
						    "PBM ranges\n");
				}
				pdev->base_address[ridx] = (unsigned long)__va(pci_addr);

				/* Preserve I/O space bit. */
				if(io) {
					pdev->base_address[ridx] |= 1;
					IO_seen = 1;
				} else {
					MEM_seen = 1;
				}
			}
		}
	}
	if(IO_seen || MEM_seen) {
		unsigned int l;

		pcibios_read_config_dword(pdev->bus->number,
					  pdev->devfn,
					  PCI_COMMAND, &l);
#ifdef FIXUP_REGS_DEBUG
		prom_printf("[");
#endif
		if(IO_seen) {
#ifdef FIXUP_REGS_DEBUG
			prom_printf("IO ");
#endif
			l |= PCI_COMMAND_IO;
		}
		if(MEM_seen) {
#ifdef FIXUP_REGS_DEBUG
			prom_printf("MEM");
#endif
			l |= PCI_COMMAND_MEMORY;
		}
#ifdef FIXUP_REGS_DEBUG
		prom_printf("]");
#endif
		pcibios_write_config_dword(pdev->bus->number,
					   pdev->devfn,
					   PCI_COMMAND, l);
	}

#ifdef FIXUP_REGS_DEBUG
	prom_printf("REG_FIXUP[%s]: ", pci_strdev(pdev->vendor, pdev->device));
	for(preg = 0; preg < 6; preg++) {
		if(pdev->base_address[preg] != 0)
			prom_printf("%d[%016lx] ", preg, pdev->base_address[preg]);
	}
	prom_printf("\n");
#endif
}

#define imap_offset(__member) \
	((unsigned long)(&(((struct psycho_regs *)0)->__member)))

static unsigned long psycho_pcislot_imap_offset(unsigned long ino)
{
	unsigned int bus, slot;

	bus = (ino & 0x10) >> 4;
	slot = (ino & 0x0c) >> 2;

	if(bus == 0) {
		/* Perform a sanity check, we might as well.
		 * PBM A only has 2 PCI slots.
		 */
		if(slot > 1) {
			prom_printf("pcislot_imap: Bogus slot on PBM A (%ld)\n", slot);
			prom_halt();
		}
		if(slot == 0)
			return imap_offset(imap_a_slot0);
		else
			return imap_offset(imap_a_slot1);
	} else {
		switch(slot) {
		case 0:
			return imap_offset(imap_b_slot0);
		case 1:
			return imap_offset(imap_b_slot1);
		case 2:
			return imap_offset(imap_b_slot2);
		case 3:
			return imap_offset(imap_b_slot3);
		default:
			prom_printf("pcislot_imap: IMPOSSIBLE [%d:%d]\n",
				    bus, slot);
			prom_halt();
			return 0; /* Make gcc happy */
		};
	}
}

/* Exported for EBUS probing layer. */
unsigned int psycho_irq_build(unsigned int full_ino)
{
	unsigned long imap_off, ign, ino;

	ign = (full_ino & PSYCHO_IMAP_IGN) >> 6;
	ino = (full_ino & PSYCHO_IMAP_INO);

	/* Compute IMAP register offset, generic IRQ layer figures out
	 * the ICLR register address as this is simple given the 32-bit
	 * irq number and IMAP register address.
	 */
	if((ino & 0x20) == 0)
		imap_off = psycho_pcislot_imap_offset(ino);
	else {
		switch(ino) {
		case 0x20:
			/* Onboard SCSI. */
			imap_off = imap_offset(imap_scsi);
			break;

		case 0x21:
			/* Onboard Ethernet (ie. CheerIO/HME) */
			imap_off = imap_offset(imap_eth);
			break;

		case 0x22:
			/* Onboard Parallel Port */
			imap_off = imap_offset(imap_bpp);
			break;

		case 0x23:
			/* Audio Record */
			imap_off = imap_offset(imap_au_rec);
			break;

		case 0x24:
			/* Audio Play */
			imap_off = imap_offset(imap_au_play);
			break;

		case 0x25:
			/* Power Fail */
			imap_off = imap_offset(imap_pfail);
			break;

		case 0x26:
			/* Onboard KBD/MOUSE/SERIAL */
			imap_off = imap_offset(imap_kms);
			break;

		case 0x27:
			/* Floppy (ie. fdthree) */
			imap_off = imap_offset(imap_flpy);
			break;

		case 0x28:
			/* Spare HW INT */
			imap_off = imap_offset(imap_shw);
			break;

		case 0x29:
			/* Onboard Keyboard (only) */
			imap_off = imap_offset(imap_kbd);
			break;

		case 0x2a:
			/* Onboard Mouse (only) */
			imap_off = imap_offset(imap_ms);
			break;

		case 0x2b:
			/* Onboard Serial (only) */
			imap_off = imap_offset(imap_ser);
			break;

		case 0x32:
			/* Power Management */
			imap_off = imap_offset(imap_pmgmt);
			break;

		default:
			/* We don't expect anything else.  The other possible
			 * values are not found in PCI device nodes, and are
			 * so hardware specific that they should use DCOOKIE's
			 * anyways.
			 */
			prom_printf("psycho_irq_build: Wacky INO [%x]\n", ino);
			prom_halt();
		};
	}
	imap_off -= imap_offset(imap_a_slot0);

	return pci_irq_encode(imap_off, 0 /* XXX */, ign, ino);
}

/* #define FIXUP_IRQ_DEBUG */

static void fixup_irq(struct pci_dev *pdev,
		      struct linux_pbm_info *pbm,
		      int node)
{
	unsigned int prom_irq, portid = pbm->parent->upa_portid;
	unsigned char pci_irq_line = pdev->irq;
	int err;

#ifdef FIXUP_IRQ_DEBUG
	printk("fixup_irq[%s:%s]: ",
	       pci_strvendor(pdev->vendor),
	       pci_strdev(pdev->vendor, pdev->device));
#endif
	err = prom_getproperty(node, "interrupts", (void *)&prom_irq, sizeof(prom_irq));
	if(err == 0 || err == -1) {
		prom_printf("fixup_irq: No interrupts property for dev[%s:%s]\n",
			    pci_strvendor(pdev->vendor),
			    pci_strdev(pdev->vendor, pdev->device));
		prom_halt();
	}

	/* See if fully specified already (ie. for onboard devices like hme) */
	if(((prom_irq & PSYCHO_IMAP_IGN) >> 6) == pbm->parent->upa_portid) {
		pdev->irq = psycho_irq_build(prom_irq);
#ifdef FIXUP_IRQ_DEBUG
		printk("fully specified prom_irq[%x] pdev->irq[%x]",
		       prom_irq, pdev->irq);
#endif
	} else {
		unsigned int bus, slot, line;

		bus = (pbm == &pbm->parent->pbm_B) ? (1 << 4) : 0;
		line = (pci_irq_line) & 3;

		/* Slot determination is only slightly complex.  Handle
		 * the easy case first.
		 */
		if(pdev->bus->number == pbm->pci_first_busno) {
			if(pbm == &pbm->parent->pbm_A)
				slot = (pdev->devfn >> 3) - 1;
			else
				slot = ((pdev->devfn >> 3) >> 1) - 1;
		} else {
			/* Underneath a bridge, use slot number of parent
			 * bridge.
			 */
			slot = (pdev->bus->self->devfn >> 3) - 1;

			/* Use low slot number bits of child as IRQ line. */
			line = ((pdev->devfn >> 3) & 3);
		}
		slot = (slot << 2);

		pdev->irq = psycho_irq_build((((portid << 6) & PSYCHO_IMAP_IGN) |
					     (bus | slot | line)));
#ifdef FIXUP_IRQ_DEBUG
		do {
			unsigned char iline, ipin;

			(void)pcibios_read_config_byte(pdev->bus->number,
						       pdev->devfn,
						       PCI_INTERRUPT_PIN,
						       &ipin);
			(void)pcibios_read_config_byte(pdev->bus->number,
						       pdev->devfn,
						       PCI_INTERRUPT_LINE,
						       &iline);
			printk("FIXED portid[%x] bus[%x] slot[%x] line[%x] irq[%x] "
			       "iline[%x] ipin[%x] prom_irq[%x]",
			       portid, bus>>4, slot>>2, line, pdev->irq,
			       iline, ipin, prom_irq);
		} while(0);
#endif
	}
#ifdef FIXUP_IRQ_DEBUG
	printk("\n");
#endif
}

static void fixup_doit(struct pci_dev *pdev,
		       struct linux_pbm_info *pbm,
		       struct linux_prom_pci_registers *pregs,
		       int nregs,
		       int node)
{
	struct linux_prom_pci_registers assigned[PROMREG_MAX];
	int numaa, err;

	/* Get assigned addresses, if any. */
	err = prom_getproperty(node, "assigned-addresses",
			       (char *)&assigned[0], sizeof(assigned));
	if(err == 0 || err == -1)
		numaa = 0;
	else
		numaa = (err / sizeof(struct linux_prom_pci_registers));

	/* First, scan and fixup base registers. */
	fixup_regs(pdev, pbm, pregs, nregs, &assigned[0], numaa);

	/* Next, fixup interrupt numbers. */
	fixup_irq(pdev, pbm, node);
}

static void fixup_pci_dev(struct pci_dev *pdev,
			  struct pci_bus *pbus,
			  struct linux_pbm_info *pbm)
{
	struct linux_prom_pci_registers pregs[PROMREG_MAX];
	struct pcidev_cookie *pcp = pdev->sysdata;
	int node, nregs, err;

	/* If this is a PCI bridge, we must program it. */
	if(pdev->class >> 8 == PCI_CLASS_BRIDGE_PCI) {
		unsigned short cmd;

		/* First, enable bus mastering. */
		pcibios_read_config_word(pdev->bus->number,
					 pdev->devfn,
					 PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MASTER;
		pcibios_write_config_word(pdev->bus->number,
					  pdev->devfn,
					  PCI_COMMAND, cmd);

		/* Now, set cache line size to 64-bytes. */
		pcibios_write_config_byte(pdev->bus->number,
					  pdev->devfn,
					  PCI_CACHE_LINE_SIZE, 64);
	}

	/* Ignore if this is one of the PBM's, EBUS, or a
	 * sub-bridge underneath the PBM.  We only need to fixup
	 * true devices.
	 */
	if((pdev->class >> 8 == PCI_CLASS_BRIDGE_PCI) ||
	   (pdev->class >> 8 == PCI_CLASS_BRIDGE_HOST) ||
	   (pdev->class >> 8 == PCI_CLASS_BRIDGE_OTHER) ||
	   (pcp == NULL))
		return;

	node = pcp->prom_node;

	err = prom_getproperty(node, "reg", (char *)&pregs[0], sizeof(pregs));
	if(err == 0 || err == -1) {
		prom_printf("Cannot find REG for pci_dev\n");
		prom_halt();
	}

	nregs = (err / sizeof(pregs[0]));

	fixup_doit(pdev, pbm, &pregs[0], nregs, node);
}

static void fixup_pci_bus(struct pci_bus *pbus, struct linux_pbm_info *pbm)
{
	struct pci_dev *pdev;

	for(pdev = pbus->devices; pdev; pdev = pdev->sibling)
		fixup_pci_dev(pdev, pbus, pbm);

	for(pbus = pbus->children; pbus; pbus = pbus->children)
		fixup_pci_bus(pbus, pbm);
}

static void fixup_addr_irq(struct linux_pbm_info *pbm)
{
	struct pci_bus *pbus = &pbm->pci_bus;

	/* Work through top level devices (not bridges, those and their
	 * devices are handled specially in the next loop).
	 */
	fixup_pci_bus(pbus, pbm);
}

/* Walk all PCI devices probes, fixing up base registers and IRQ registers.
 * We use OBP for most of this work.
 */
static void psycho_final_fixup(struct linux_psycho *psycho)
{
	/* Second, fixup base address registers and IRQ lines... */
	fixup_addr_irq(&psycho->pbm_A);
	fixup_addr_irq(&psycho->pbm_B);

#if 0
	prom_halt();
#endif
}

unsigned long pcibios_fixup(unsigned long memory_start, unsigned long memory_end)
{
	struct linux_psycho *psycho = psycho_root;

	pci_probe_enable = 1;

	/* XXX Really this should be per-PSYCHO, but the config space
	 * XXX reads and writes give us no way to know which PSYCHO
	 * XXX in which the config space reads should occur.
	 * XXX
	 * XXX Further thought says that we cannot change this generic
	 * XXX interface, else we'd break xfree86 and other parts of the
	 * XXX kernel (but whats more important is breaking userland for
	 * XXX the ix86/Alpha/etc. people).  So we should define our own
	 * XXX internal extension initially, we can compile our own user
	 * XXX apps that need to get at PCI configuration space.
	 */

	/* Probe busses under PBM A. */
	pbm_probe(&psycho->pbm_A, &memory_start);

	/* Probe busses under PBM B. */
	pbm_probe(&psycho->pbm_B, &memory_start);

	pci_init_alloc_init(&memory_start);

	/* Walk all PCI devices found.  For each device, and
	 * PCI bridge which is not one of the PSYCHO PBM's, fill in the
	 * sysdata with a pointer to the PBM (for pci_bus's) or
	 * a pci_dev cookie (PBM+PROM_NODE, for pci_dev's).
	 */
	fill_in_pbm_cookies(&psycho->pbm_A);
	fill_in_pbm_cookies(&psycho->pbm_B);

	/* See what OBP has taken care of already. */
	record_assignments(&psycho->pbm_A);
	record_assignments(&psycho->pbm_B);

	/* Now, fix it all up. */
	psycho_final_fixup(psycho);

	pci_init_alloc_fini();

	return ebus_init(memory_start, memory_end);
}

/* "PCI: The emerging standard..." 8-( */
volatile int pci_poke_in_progress = 0;
volatile int pci_poke_faulted = 0;

/* XXX Current PCI support code is broken, it assumes one master PCI config
 * XXX space exists, on Ultra we can have many of them, especially with
 * XXX 'dual-pci' boards on Sunfire/Starfire/Wildfire.
 */
static char *pci_mkaddr(unsigned char bus, unsigned char device_fn,
			unsigned char where)
{
	unsigned long ret = (unsigned long) psycho_root->pci_config_space;

	ret |= (1 << 24);
	ret |= ((bus & 0xff) << 16);
	ret |= ((device_fn & 0xff) << 8);
	ret |= (where & 0xfc);
	return (unsigned char *)ret;
}

static inline int out_of_range(unsigned char bus, unsigned char device_fn)
{
	return ((bus == 0 && PCI_SLOT(device_fn) > 4) ||
		(bus == 1 && PCI_SLOT(device_fn) > 6) ||
		(pci_probe_enable == 0));
}

int pcibios_read_config_byte (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char *value)
{
	unsigned char *addr = pci_mkaddr(bus, device_fn, where);
	unsigned int word, trapped;

	*value = 0xff;

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduwa [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (word)
			     : "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	trapped = pci_poke_faulted;
	pci_poke_faulted = 0;
	if(!trapped) {
		switch(where & 3) {
		case 0:
			*value = word & 0xff;
			break;
		case 1:
			*value = (word >> 8) & 0xff;
			break;
		case 2:
			*value = (word >> 16) & 0xff;
			break;
		case 3:
			*value = (word >> 24) & 0xff;
			break;
		};
	}
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_word (unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned short *value)
{
	unsigned short *addr = (unsigned short *)pci_mkaddr(bus, device_fn, where);
	unsigned int word, trapped;

	*value = 0xffff;

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduwa [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (word)
			     : "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	trapped = pci_poke_faulted;
	pci_poke_faulted = 0;
	if(!trapped) {
		switch(where & 3) {
		case 0:
			*value = word & 0xffff;
			break;
		case 2:
			*value = (word >> 16) & 0xffff;
			break;
		default:
			printk("pcibios_read_config_word: misaligned "
			       "reg [%x]\n", where);
			break;
		};
	}
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_dword (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned int *value)
{
	unsigned int *addr = (unsigned int *)pci_mkaddr(bus, device_fn, where);
	unsigned int word, trapped;

	*value = 0xffffffff;

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduwa [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (word)
			     : "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	trapped = pci_poke_faulted;
	pci_poke_faulted = 0;
	if(!trapped)
		*value = word;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_byte (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned char value)
{
	unsigned char *addr = pci_mkaddr(bus, device_fn, where);

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;

	/* Endianness doesn't matter but we have to get the memory
	 * barriers in there so...
	 */
	__asm__ __volatile__("membar #Sync\n\t"
			     "stba %0, [%1] %2\n\t"
			     "membar #Sync\n\t"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;

	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_word (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned short value)
{
	unsigned short *addr = (unsigned short *)pci_mkaddr(bus, device_fn, where);

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stha %0, [%1] %2\n\t"
			     "membar #Sync\n\t"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_dword (unsigned char bus, unsigned char device_fn,
				unsigned char where, unsigned int value)
{
	unsigned int *addr = (unsigned int *)pci_mkaddr(bus, device_fn, where);

	if(out_of_range(bus, device_fn))
		return PCIBIOS_SUCCESSFUL;

	pci_poke_in_progress = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stwa %0, [%1] %2\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (value), "r" (addr), "i" (ASI_PL));
	pci_poke_in_progress = 0;
	return PCIBIOS_SUCCESSFUL;
}

asmlinkage int sys_pciconfig_read(unsigned long bus,
				  unsigned long dfn,
				  unsigned long off,
				  unsigned long len,
				  unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	int err = 0;

	lock_kernel();
	switch(len) {
	case 1:
		pcibios_read_config_byte(bus, dfn, off, &ubyte);
		put_user(ubyte, buf);
		break;
	case 2:
		pcibios_read_config_word(bus, dfn, off, &ushort);
		put_user(ushort, buf);
		break;
	case 4:
		pcibios_read_config_dword(bus, dfn, off, &uint);
		put_user(uint, buf);
		break;

	default:
		err = -EINVAL;
		break;
	};
	unlock_kernel();

	return err;
}

asmlinkage int sys_pciconfig_write(unsigned long bus,
				   unsigned long dfn,
				   unsigned long off,
				   unsigned long len,
				   unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	int err = 0;

	lock_kernel();
	switch(len) {
	case 1:
		err = get_user(ubyte, (unsigned char *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, ubyte);
		break;

	case 2:
		err = get_user(ushort, (unsigned short *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, ushort);
		break;

	case 4:
		err = get_user(uint, (unsigned int *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, uint);
		break;

	default:
		err = -EINVAL;
		break;

	};
	unlock_kernel();

	return err;
}

#endif