/*
 * AGPGART module version 0.99
 * Copyright (C) 1999 Jeff Hartmann
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
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
 * JEFF HARTMANN, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/miscdevice.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>

#include <linux/agp_backend.h>
#include "agp.h"

MODULE_AUTHOR("Jeff Hartmann <jhartmann@precisioninsight.com>");
MODULE_PARM(agp_try_unsupported, "1i");
EXPORT_SYMBOL(agp_free_memory);
EXPORT_SYMBOL(agp_allocate_memory);
EXPORT_SYMBOL(agp_copy_info);
EXPORT_SYMBOL(agp_bind_memory);
EXPORT_SYMBOL(agp_unbind_memory);
EXPORT_SYMBOL(agp_enable);
EXPORT_SYMBOL(agp_backend_acquire);
EXPORT_SYMBOL(agp_backend_release);

static void flush_cache(void);

static struct agp_bridge_data agp_bridge;
static int agp_try_unsupported __initdata = 0;


static inline void flush_cache(void)
{
#if defined(__i386__)
	asm volatile ("wbinvd":::"memory");
#elif defined(__alpha__)
	/* ??? I wonder if we'll really need to flush caches, or if the
	   core logic can manage to keep the system coherent.  The ARM
	   speaks only of using `cflush' to get things in memory in
	   preparation for power failure.

	   If we do need to call `cflush', we'll need a target page,
	   as we can only flush one page at a time.  */
	mb();
#else
#error "Please define flush_cache."
#endif
}

#ifdef __SMP__
static atomic_t cpus_waiting;

static void ipi_handler(void *null)
{
	flush_cache();
	atomic_dec(&cpus_waiting);
	while (atomic_read(&cpus_waiting) > 0)
		barrier();
}

static void smp_flush_cache(void)
{
	atomic_set(&cpus_waiting, smp_num_cpus - 1);
	if (smp_call_function(ipi_handler, NULL, 1, 0) != 0)
		panic("agpgart: timed out waiting for the other CPUs!\n");
	flush_cache();
	while (atomic_read(&cpus_waiting) > 0)
		barrier();
}
#define global_cache_flush smp_flush_cache
#else				/* __SMP__ */
#define global_cache_flush flush_cache
#endif				/* __SMP__ */

int agp_backend_acquire(void)
{
	atomic_inc(&agp_bridge.agp_in_use);

	if (atomic_read(&agp_bridge.agp_in_use) != 1) {
		atomic_dec(&agp_bridge.agp_in_use);
		return -EBUSY;
	}
	MOD_INC_USE_COUNT;
	return 0;
}

void agp_backend_release(void)
{
	atomic_dec(&agp_bridge.agp_in_use);
	MOD_DEC_USE_COUNT;
}

/* 
 * Basic Page Allocation Routines -
 * These routines handle page allocation
 * and by default they reserve the allocated 
 * memory.  They also handle incrementing the
 * current_memory_agp value, Which is checked
 * against a maximum value.
 */

static unsigned long agp_alloc_page(void)
{
	void *pt;

	pt = (void *) __get_free_page(GFP_KERNEL);
	if (pt == NULL) {
		return 0;
	}
	atomic_inc(&mem_map[MAP_NR(pt)].count);
	set_bit(PG_locked, &mem_map[MAP_NR(pt)].flags);
	atomic_inc(&agp_bridge.current_memory_agp);
	return (unsigned long) pt;
}

static void agp_destroy_page(unsigned long page)
{
	void *pt = (void *) page;

	if (pt == NULL) {
		return;
	}
	atomic_dec(&mem_map[MAP_NR(pt)].count);
	clear_bit(PG_locked, &mem_map[MAP_NR(pt)].flags);
	wake_up(&mem_map[MAP_NR(pt)].wait);
	free_page((unsigned long) pt);
	atomic_dec(&agp_bridge.current_memory_agp);
}

/* End Basic Page Allocation Routines */

/* 
 * Generic routines for handling agp_memory structures -
 * They use the basic page allocation routines to do the
 * brunt of the work.
 */


static void agp_free_key(int key)
{

	if (key < 0) {
		return;
	}
	if (key < MAXKEY) {
		clear_bit(key, agp_bridge.key_list);
	}
}

static int agp_get_key(void)
{
	int bit;

	bit = find_first_zero_bit(agp_bridge.key_list, MAXKEY);
	if (bit < MAXKEY) {
		set_bit(bit, agp_bridge.key_list);
		return bit;
	}
	return -1;
}

static agp_memory *agp_create_memory(int scratch_pages)
{
	agp_memory *new;

	new = kmalloc(sizeof(agp_memory), GFP_KERNEL);

	if (new == NULL) {
		return NULL;
	}
	memset(new, 0, sizeof(agp_memory));
	new->key = agp_get_key();

	if (new->key < 0) {
		kfree(new);
		return NULL;
	}
	new->memory = vmalloc(PAGE_SIZE * scratch_pages);

	if (new->memory == NULL) {
		agp_free_key(new->key);
		kfree(new);
		return NULL;
	}
	new->num_scratch_pages = scratch_pages;
	return new;
}

void agp_free_memory(agp_memory * curr)
{
	int i;

	if (curr == NULL) {
		return;
	}
	if (curr->is_bound == TRUE) {
		agp_unbind_memory(curr);
	}
	if (curr->type != 0) {
		agp_bridge.free_by_type(curr);
		MOD_DEC_USE_COUNT;
		return;
	}
	if (curr->page_count != 0) {
		for (i = 0; i < curr->page_count; i++) {
			curr->memory[i] &= ~(0x00000fff);
			agp_destroy_page((unsigned long)
					 phys_to_virt(curr->memory[i]));
		}
	}
	agp_free_key(curr->key);
	vfree(curr->memory);
	kfree(curr);
	MOD_DEC_USE_COUNT;
}

#define ENTRIES_PER_PAGE		(PAGE_SIZE / sizeof(unsigned long))

agp_memory *agp_allocate_memory(size_t page_count, u32 type)
{
	int scratch_pages;
	agp_memory *new;
	int i;

	if ((atomic_read(&agp_bridge.current_memory_agp) + page_count) >
	    agp_bridge.max_memory_agp) {
		return NULL;
	}
	if (type != 0) {
		new = agp_bridge.alloc_by_type(page_count, type);
		return new;
	}
	scratch_pages = (page_count + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;

	new = agp_create_memory(scratch_pages);

	if (new == NULL) {
		return NULL;
	}
	for (i = 0; i < page_count; i++) {
		new->memory[i] = agp_alloc_page();

		if (new->memory[i] == 0) {
			/* Free this structure */
			agp_free_memory(new);
			return NULL;
		}
		new->memory[i] =
		    agp_bridge.mask_memory(
				   virt_to_phys((void *) new->memory[i]),
						  type);
		new->page_count++;
	}

	MOD_INC_USE_COUNT;
	return new;
}

/* End - Generic routines for handling agp_memory structures */

static int agp_return_size(void)
{
	int current_size;
	void *temp;

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
	case U8_APER_SIZE:
		current_size = A_SIZE_8(temp)->size;
		break;
	case U16_APER_SIZE:
		current_size = A_SIZE_16(temp)->size;
		break;
	case U32_APER_SIZE:
		current_size = A_SIZE_32(temp)->size;
		break;
	case FIXED_APER_SIZE:
		current_size = A_SIZE_FIX(temp)->size;
		break;
	default:
		current_size = 0;
		break;
	}

	return current_size;
}

/* Routine to copy over information structure */

void agp_copy_info(agp_kern_info * info)
{
	memset(info, 0, sizeof(agp_kern_info));
	info->version.major = agp_bridge.version->major;
	info->version.minor = agp_bridge.version->minor;
	info->device = agp_bridge.dev;
	info->chipset = agp_bridge.type;
	info->mode = agp_bridge.mode;
	info->aper_base = agp_bridge.gart_bus_addr;
	info->aper_size = agp_return_size();
	info->max_memory = agp_bridge.max_memory_agp;
	info->current_memory = atomic_read(&agp_bridge.current_memory_agp);
}

/* End - Routine to copy over information structure */

/*
 * Routines for handling swapping of agp_memory into the GATT -
 * These routines take agp_memory and insert them into the GATT.
 * They call device specific routines to actually write to the GATT.
 */

int agp_bind_memory(agp_memory * curr, off_t pg_start)
{
	int ret_val;

	if ((curr == NULL) || (curr->is_bound == TRUE)) {
		return -EINVAL;
	}
	if (curr->is_flushed == FALSE) {
		CACHE_FLUSH();
		curr->is_flushed = TRUE;
	}
	ret_val = agp_bridge.insert_memory(curr, pg_start, curr->type);

	if (ret_val != 0) {
		return ret_val;
	}
	curr->is_bound = TRUE;
	curr->pg_start = pg_start;
	return 0;
}

int agp_unbind_memory(agp_memory * curr)
{
	int ret_val;

	if (curr == NULL) {
		return -EINVAL;
	}
	if (curr->is_bound != TRUE) {
		return -EINVAL;
	}
	ret_val = agp_bridge.remove_memory(curr, curr->pg_start, curr->type);

	if (ret_val != 0) {
		return ret_val;
	}
	curr->is_bound = FALSE;
	curr->pg_start = 0;
	return 0;
}

/* End - Routines for handling swapping of agp_memory into the GATT */

/* 
 * Driver routines - start
 * Currently this module supports the 
 * i810, 440lx, 440bx, 440gx, via vp3, via mvp3,
 * amd irongate, ALi M1541 and generic support for the
 * SiS chipsets.
 */

/* Generic Agp routines - Start */

static void agp_generic_agp_enable(u32 mode)
{
	struct pci_dev *device = NULL;
	u32 command, scratch, cap_id;
	u8 cap_ptr;

	pci_read_config_dword(agp_bridge.dev,
			      agp_bridge.capndx + 4,
			      &command);

	/*
	 * PASS1: go throu all devices that claim to be
	 *        AGP devices and collect their data.
	 */

	while ((device = pci_find_class(PCI_CLASS_DISPLAY_VGA << 8,
					device)) != NULL) {
		pci_read_config_dword(device, 0x04, &scratch);

		if (!(scratch & 0x00100000))
			continue;

		pci_read_config_byte(device, 0x34, &cap_ptr);

		if (cap_ptr != 0x00) {
			do {
				pci_read_config_dword(device,
						      cap_ptr, &cap_id);

				if ((cap_id & 0xff) != 0x02)
					cap_ptr = (cap_id >> 8) & 0xff;
			}
			while (((cap_id & 0xff) != 0x02) && (cap_ptr != 0x00));
		}
		if (cap_ptr != 0x00) {
			/*
			 * Ok, here we have a AGP device. Disable impossible 
			 * settings, and adjust the readqueue to the minimum.
			 */

			pci_read_config_dword(device, cap_ptr + 4, &scratch);

			/* adjust RQ depth */
			command =
			    ((command & ~0xff000000) |
			     min((mode & 0xff000000),
				 min((command & 0xff000000),
				     (scratch & 0xff000000))));

			/* disable SBA if it's not supported */
			if (!((command & 0x00000200) &&
			      (scratch & 0x00000200) &&
			      (mode & 0x00000200)))
				command &= ~0x00000200;

			/* disable FW if it's not supported */
			if (!((command & 0x00000010) &&
			      (scratch & 0x00000010) &&
			      (mode & 0x00000010)))
				command &= ~0x00000010;

			if (!((command & 4) &&
			      (scratch & 4) &&
			      (mode & 4)))
				command &= ~0x00000004;

			if (!((command & 2) &&
			      (scratch & 2) &&
			      (mode & 2)))
				command &= ~0x00000002;

			if (!((command & 1) &&
			      (scratch & 1) &&
			      (mode & 1)))
				command &= ~0x00000001;
		}
	}
	/*
	 * PASS2: Figure out the 4X/2X/1X setting and enable the
	 *        target (our motherboard chipset).
	 */

	if (command & 4) {
		command &= ~3;	/* 4X */
	}
	if (command & 2) {
		command &= ~5;	/* 2X */
	}
	if (command & 1) {
		command &= ~6;	/* 1X */
	}
	command |= 0x00000100;

	pci_write_config_dword(agp_bridge.dev,
			       agp_bridge.capndx + 8,
			       command);

	/*
	 * PASS3: Go throu all AGP devices and update the
	 *        command registers.
	 */

	while ((device = pci_find_class(PCI_CLASS_DISPLAY_VGA << 8,
					device)) != NULL) {
		pci_read_config_dword(device, 0x04, &scratch);

		if (!(scratch & 0x00100000))
			continue;

		pci_read_config_byte(device, 0x34, &cap_ptr);

		if (cap_ptr != 0x00) {
			do {
				pci_read_config_dword(device,
						      cap_ptr, &cap_id);

				if ((cap_id & 0xff) != 0x02)
					cap_ptr = (cap_id >> 8) & 0xff;
			}
			while (((cap_id & 0xff) != 0x02) && (cap_ptr != 0x00));
		}
		if (cap_ptr != 0x00)
			pci_write_config_dword(device, cap_ptr + 8, command);
	}
}

static int agp_generic_create_gatt_table(void)
{
	char *table;
	char *table_end;
	int size;
	int page_order;
	int num_entries;
	int i;
	void *temp;

	table = NULL;
	i = agp_bridge.aperture_size_idx;
	temp = agp_bridge.current_size;
	size = page_order = num_entries = 0;

	if (agp_bridge.size_type != FIXED_APER_SIZE) {
		do {
			switch (agp_bridge.size_type) {
			case U8_APER_SIZE:
				size = A_SIZE_8(temp)->size;
				page_order =
				    A_SIZE_8(temp)->page_order;
				num_entries =
				    A_SIZE_8(temp)->num_entries;
				break;
			case U16_APER_SIZE:
				size = A_SIZE_16(temp)->size;
				page_order = A_SIZE_16(temp)->page_order;
				num_entries = A_SIZE_16(temp)->num_entries;
				break;
			case U32_APER_SIZE:
				size = A_SIZE_32(temp)->size;
				page_order = A_SIZE_32(temp)->page_order;
				num_entries = A_SIZE_32(temp)->num_entries;
				break;
				/* This case will never really happen. */
			case FIXED_APER_SIZE:
			default:
				size = page_order = num_entries = 0;
				break;
			}

			table = (char *) __get_free_pages(GFP_KERNEL,
							  page_order);

			if (table == NULL) {
				i++;
				switch (agp_bridge.size_type) {
				case U8_APER_SIZE:
					agp_bridge.current_size = A_IDX8();
					break;
				case U16_APER_SIZE:
					agp_bridge.current_size = A_IDX16();
					break;
				case U32_APER_SIZE:
					agp_bridge.current_size = A_IDX32();
					break;
					/* This case will never really 
					 * happen. 
					 */
				case FIXED_APER_SIZE:
				default:
					agp_bridge.current_size =
					    agp_bridge.current_size;
					break;
				}
			} else {
				agp_bridge.aperture_size_idx = i;
			}
		} while ((table == NULL) &&
			 (i < agp_bridge.num_aperture_sizes));
	} else {
		size = ((aper_size_info_fixed *) temp)->size;
		page_order = ((aper_size_info_fixed *) temp)->page_order;
		num_entries = ((aper_size_info_fixed *) temp)->num_entries;
		table = (char *) __get_free_pages(GFP_KERNEL, page_order);
	}

	if (table == NULL) {
		return -ENOMEM;
	}
	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (i = MAP_NR(table); i < MAP_NR(table_end); i++) {
		set_bit(PG_reserved, &mem_map[i].flags);
	}

	agp_bridge.gatt_table_real = (unsigned long *) table;
	CACHE_FLUSH();
	agp_bridge.gatt_table = ioremap_nocache(virt_to_phys(table),
					(PAGE_SIZE * (1 << page_order)));
	CACHE_FLUSH();

	if (agp_bridge.gatt_table == NULL) {
		for (i = MAP_NR(table); i < MAP_NR(table_end); i++) {
			clear_bit(PG_reserved, &mem_map[i].flags);
		}

		free_pages((unsigned long) table, page_order);

		return -ENOMEM;
	}
	agp_bridge.gatt_bus_addr = virt_to_phys(agp_bridge.gatt_table_real);

	for (i = 0; i < num_entries; i++) {
		agp_bridge.gatt_table[i] =
		    (unsigned long) agp_bridge.scratch_page;
	}

	return 0;
}

static int agp_generic_free_gatt_table(void)
{
	int i;
	int page_order;
	char *table, *table_end;
	void *temp;

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
	case U8_APER_SIZE:
		page_order = A_SIZE_8(temp)->page_order;
		break;
	case U16_APER_SIZE:
		page_order = A_SIZE_16(temp)->page_order;
		break;
	case U32_APER_SIZE:
		page_order = A_SIZE_32(temp)->page_order;
		break;
	case FIXED_APER_SIZE:
		page_order = A_SIZE_FIX(temp)->page_order;
		break;
	default:
		page_order = 0;
		break;
	}

	/* Do not worry about freeing memory, because if this is
	 * called, then all agp memory is deallocated and removed
	 * from the table.
	 */

	iounmap(agp_bridge.gatt_table);
	table = (char *) agp_bridge.gatt_table_real;
	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (i = MAP_NR(table); i < MAP_NR(table_end); i++) {
		clear_bit(PG_reserved, &mem_map[i].flags);
	}

	free_pages((unsigned long) agp_bridge.gatt_table_real, page_order);
	return 0;
}

static int agp_generic_insert_memory(agp_memory * mem,
				     off_t pg_start, int type)
{
	int i, j, num_entries;
	void *temp;

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
	case U8_APER_SIZE:
		num_entries = A_SIZE_8(temp)->num_entries;
		break;
	case U16_APER_SIZE:
		num_entries = A_SIZE_16(temp)->num_entries;
		break;
	case U32_APER_SIZE:
		num_entries = A_SIZE_32(temp)->num_entries;
		break;
	case FIXED_APER_SIZE:
		num_entries = A_SIZE_FIX(temp)->num_entries;
		break;
	default:
		num_entries = 0;
		break;
	}

	if (type != 0 || mem->type != 0) {
		/* The generic routines know nothing of memory types */
		return -EINVAL;
	}
	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}
	j = pg_start;

	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j])) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		agp_bridge.gatt_table[j] = mem->memory[i];
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static int agp_generic_remove_memory(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i;

	if (type != 0 || mem->type != 0) {
		/* The generic routines know nothing of memory types */
		return -EINVAL;
	}
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		agp_bridge.gatt_table[i] =
		    (unsigned long) agp_bridge.scratch_page;
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static agp_memory *agp_generic_alloc_by_type(size_t page_count, int type)
{
	return NULL;
}

static void agp_generic_free_by_type(agp_memory * curr)
{
	if (curr->memory != NULL) {
		vfree(curr->memory);
	}
	agp_free_key(curr->key);
	kfree(curr);
}

void agp_enable(u32 mode)
{
	agp_bridge.agp_enable(mode);
}

/* End - Generic Agp routines */

#ifdef CONFIG_AGP_I810
static aper_size_info_fixed intel_i810_sizes[] =
{
	{64, 16384, 4},
     /* The 32M mode still requires a 64k gatt */
	{32, 8192, 4}
};

#define AGP_DCACHE_MEMORY 1

static gatt_mask intel_i810_masks[] =
{
	{I810_PTE_VALID, 0},
	{(I810_PTE_VALID | I810_PTE_LOCAL), AGP_DCACHE_MEMORY}
};

static struct _intel_i810_private {
	struct pci_dev *i810_dev;	/* device one */
	volatile u8 *registers;
	int num_dcache_entries;
} intel_i810_private;

static int intel_i810_fetch_size(void)
{
	u32 smram_miscc;
	aper_size_info_fixed *values;

	pci_read_config_dword(agp_bridge.dev, I810_SMRAM_MISCC, &smram_miscc);
	values = A_SIZE_FIX(agp_bridge.aperture_sizes);

	if ((smram_miscc & I810_GMS) == I810_GMS_DISABLE) {
		printk("agpgart: i810 is disabled\n");
		return 0;
	}
	if ((smram_miscc & I810_GFX_MEM_WIN_SIZE) == I810_GFX_MEM_WIN_32M) {
		agp_bridge.previous_size =
		    agp_bridge.current_size = (void *) (values + 1);
		agp_bridge.aperture_size_idx = 1;
		return values[1].size;
	} else {
		agp_bridge.previous_size =
		    agp_bridge.current_size = (void *) (values);
		agp_bridge.aperture_size_idx = 0;
		return values[0].size;
	}

	return 0;
}

static int intel_i810_configure(void)
{
	aper_size_info_fixed *current_size;
	u32 temp;
	int i;

	current_size = A_SIZE_FIX(agp_bridge.current_size);

	pci_read_config_dword(intel_i810_private.i810_dev, I810_MMADDR, &temp);
	temp &= 0xfff80000;

	intel_i810_private.registers =
	    (volatile u8 *) ioremap(temp, 128 * 4096);

	if ((INREG32(intel_i810_private.registers, I810_DRAM_CTL)
	     & I810_DRAM_ROW_0) == I810_DRAM_ROW_0_SDRAM) {
		/* This will need to be dynamically assigned */
		printk(KERN_INFO
		       "agpgart: detected 4MB dedicated video ram.\n");
		intel_i810_private.num_dcache_entries = 1024;
	}
	pci_read_config_dword(intel_i810_private.i810_dev, I810_GMADDR, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	OUTREG32(intel_i810_private.registers, I810_PGETBL_CTL,
		 agp_bridge.gatt_bus_addr | I810_PGETBL_ENABLED);
	CACHE_FLUSH();

	if (agp_bridge.needs_scratch_page == TRUE) {
		for (i = 0; i < current_size->num_entries; i++) {
			OUTREG32(intel_i810_private.registers,
				 I810_PTE_BASE + (i * 4),
				 agp_bridge.scratch_page);
		}
	}
	return 0;
}

static void intel_i810_cleanup(void)
{
	OUTREG32(intel_i810_private.registers, I810_PGETBL_CTL, 0);
	iounmap((void *) intel_i810_private.registers);
}

static void intel_i810_tlbflush(agp_memory * mem)
{
	return;
}

static void intel_i810_agp_enable(u32 mode)
{
	return;
}

static int intel_i810_insert_entries(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i, j, num_entries;
	void *temp;

	temp = agp_bridge.current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}
	for (j = pg_start; j < (pg_start + mem->page_count); j++) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j])) {
			return -EBUSY;
		}
	}

	if (type != 0 || mem->type != 0) {
		if ((type == AGP_DCACHE_MEMORY) &&
		    (mem->type == AGP_DCACHE_MEMORY)) {
			/* special insert */

			for (i = pg_start;
			     i < (pg_start + mem->page_count); i++) {
				OUTREG32(intel_i810_private.registers,
					 I810_PTE_BASE + (i * 4),
					 (i * 4096) | I810_PTE_LOCAL |
					 I810_PTE_VALID);
			}

			agp_bridge.tlb_flush(mem);
			return 0;
		}
		return -EINVAL;
	}
	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		OUTREG32(intel_i810_private.registers,
			 I810_PTE_BASE + (j * 4), mem->memory[i]);
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static int intel_i810_remove_entries(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i;

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		OUTREG32(intel_i810_private.registers,
			 I810_PTE_BASE + (i * 4),
			 agp_bridge.scratch_page);
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static agp_memory *intel_i810_alloc_by_type(size_t pg_count, int type)
{
	agp_memory *new;

	if (type == AGP_DCACHE_MEMORY) {
		if (pg_count != intel_i810_private.num_dcache_entries) {
			return NULL;
		}
		new = agp_create_memory(1);

		if (new == NULL) {
			return NULL;
		}
		new->type = AGP_DCACHE_MEMORY;
		new->page_count = pg_count;
		new->num_scratch_pages = 0;
		vfree(new->memory);
		return new;
	}
	return NULL;
}

static void intel_i810_free_by_type(agp_memory * curr)
{
	agp_free_key(curr->key);
	kfree(curr);
}

static unsigned long intel_i810_mask_memory(unsigned long addr, int type)
{
	/* Type checking must be done elsewhere */
	return addr | agp_bridge.masks[type].mask;
}

static void intel_i810_setup(struct pci_dev *i810_dev)
{
	intel_i810_private.i810_dev = i810_dev;

	agp_bridge.masks = intel_i810_masks;
	agp_bridge.num_of_masks = 2;
	agp_bridge.aperture_sizes = (void *) intel_i810_sizes;
	agp_bridge.size_type = FIXED_APER_SIZE;
	agp_bridge.num_aperture_sizes = 2;
	agp_bridge.dev_private_data = (void *) &intel_i810_private;
	agp_bridge.needs_scratch_page = TRUE;
	agp_bridge.configure = intel_i810_configure;
	agp_bridge.fetch_size = intel_i810_fetch_size;
	agp_bridge.cleanup = intel_i810_cleanup;
	agp_bridge.tlb_flush = intel_i810_tlbflush;
	agp_bridge.mask_memory = intel_i810_mask_memory;
	agp_bridge.agp_enable = intel_i810_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = intel_i810_insert_entries;
	agp_bridge.remove_memory = intel_i810_remove_entries;
	agp_bridge.alloc_by_type = intel_i810_alloc_by_type;
	agp_bridge.free_by_type = intel_i810_free_by_type;
}

#endif

#ifdef CONFIG_AGP_INTEL

static int intel_fetch_size(void)
{
	int i;
	u16 temp;
	aper_size_info_16 *values;

	pci_read_config_word(agp_bridge.dev, INTEL_APSIZE, &temp);
	values = A_SIZE_16(agp_bridge.aperture_sizes);

	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void intel_tlbflush(agp_memory * mem)
{
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2200);
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2280);
}

static void intel_cleanup(void)
{
	u16 temp;
	aper_size_info_16 *previous_size;

	previous_size = A_SIZE_16(agp_bridge.previous_size);
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG, temp & ~(1 << 9));
	pci_write_config_word(agp_bridge.dev, INTEL_APSIZE,
			      previous_size->size_value);
}

static int intel_configure(void)
{
	u32 temp;
	u16 temp2;
	aper_size_info_16 *current_size;

	current_size = A_SIZE_16(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_word(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2280);

	/* paccfg/nbxcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG,
			      (temp2 & ~(1 << 10)) | (1 << 9));
	/* clear any possible error conditions */
	pci_write_config_byte(agp_bridge.dev, INTEL_ERRSTS + 1, 7);
	return 0;
}

static unsigned long intel_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}


/* Setup function */
static gatt_mask intel_generic_masks[] =
{
	{0x00000017, 0}
};

static aper_size_info_16 intel_generic_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56},
	{16, 4096, 2, 60},
	{8, 2048, 1, 62},
	{4, 1024, 0, 63}
};

static void intel_generic_setup(void)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) intel_generic_sizes;
	agp_bridge.size_type = U16_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_configure;
	agp_bridge.fetch_size = intel_fetch_size;
	agp_bridge.cleanup = intel_cleanup;
	agp_bridge.tlb_flush = intel_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
}

#endif

#ifdef CONFIG_AGP_VIA

static int via_fetch_size(void)
{
	int i;
	u8 temp;
	aper_size_info_8 *values;

	values = A_SIZE_8(agp_bridge.aperture_sizes);
	pci_read_config_byte(agp_bridge.dev, VIA_APSIZE, &temp);
	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int via_configure(void)
{
	u32 temp;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);
	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, VIA_APSIZE,
			      current_size->size_value);
	/* address to map too */
	pci_read_config_dword(agp_bridge.dev, VIA_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* GART control register */
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000000f);

	/* attbase - aperture GATT base */
	pci_write_config_dword(agp_bridge.dev, VIA_ATTBASE,
			    (agp_bridge.gatt_bus_addr & 0xfffff000) | 3);
	return 0;
}

static void via_cleanup(void)
{
	aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_write_config_dword(agp_bridge.dev, VIA_ATTBASE, 0);
	pci_write_config_byte(agp_bridge.dev, VIA_APSIZE,
			      previous_size->size_value);
}

static void via_tlbflush(agp_memory * mem)
{
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000008f);
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000000f);
}

static unsigned long via_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static aper_size_info_8 via_generic_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 128},
	{64, 16384, 4, 192},
	{32, 8192, 3, 224},
	{16, 4096, 2, 240},
	{8, 2048, 1, 248},
	{4, 1024, 0, 252}
};

static gatt_mask via_generic_masks[] =
{
	{0x00000000, 0}
};

static void via_generic_setup(void)
{
	agp_bridge.masks = via_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) via_generic_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = via_configure;
	agp_bridge.fetch_size = via_fetch_size;
	agp_bridge.cleanup = via_cleanup;
	agp_bridge.tlb_flush = via_tlbflush;
	agp_bridge.mask_memory = via_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
}

#endif

#ifdef CONFIG_AGP_SIS

static int sis_fetch_size(void)
{
	u8 temp_size;
	int i;
	aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge.dev, SIS_APSIZE, &temp_size);
	values = A_SIZE_8(agp_bridge.aperture_sizes);
	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if ((temp_size == values[i].size_value) ||
		    ((temp_size & ~(0x03)) ==
		     (values[i].size_value & ~(0x03)))) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);

			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}


static void sis_tlbflush(agp_memory * mem)
{
	pci_write_config_byte(agp_bridge.dev, SIS_TLBFLUSH, 0x02);
}

static int sis_configure(void)
{
	u32 temp;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);
	pci_write_config_byte(agp_bridge.dev, SIS_TLBCNTRL, 0x05);
	pci_read_config_dword(agp_bridge.dev, SIS_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	pci_write_config_dword(agp_bridge.dev, SIS_ATTBASE,
			       agp_bridge.gatt_bus_addr);
	pci_write_config_byte(agp_bridge.dev, SIS_APSIZE,
			      current_size->size_value);
	return 0;
}

static void sis_cleanup(void)
{
	aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_write_config_byte(agp_bridge.dev, SIS_APSIZE,
			      (previous_size->size_value & ~(0x03)));
}

static unsigned long sis_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static aper_size_info_8 sis_generic_sizes[7] =
{
	{256, 65536, 6, 99},
	{128, 32768, 5, 83},
	{64, 16384, 4, 67},
	{32, 8192, 3, 51},
	{16, 4096, 2, 35},
	{8, 2048, 1, 19},
	{4, 1024, 0, 3}
};

static gatt_mask sis_generic_masks[] =
{
	{0x00000000, 0}
};

static void sis_generic_setup(void)
{
	agp_bridge.masks = sis_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) sis_generic_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = sis_configure;
	agp_bridge.fetch_size = sis_fetch_size;
	agp_bridge.cleanup = sis_cleanup;
	agp_bridge.tlb_flush = sis_tlbflush;
	agp_bridge.mask_memory = sis_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
}

#endif

#ifdef CONFIG_AGP_AMD

static struct _amd_irongate_private {
	volatile u8 *registers;
} amd_irongate_private;

static int amd_irongate_fetch_size(void)
{
	int i;
	u32 temp;
	aper_size_info_32 *values;

	pci_read_config_dword(agp_bridge.dev, AMD_APSIZE, &temp);
	temp = (temp & 0x0000000e);
	values = A_SIZE_32(agp_bridge.aperture_sizes);
	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);

			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int amd_irongate_configure(void)
{
	aper_size_info_32 *current_size;
	unsigned long addr;
	u32 temp;
	u16 enable_reg;

	current_size = A_SIZE_32(agp_bridge.current_size);

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge.dev, AMD_MMBASE, &temp);
	temp = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	amd_irongate_private.registers = (volatile u8 *) ioremap(temp, 4096);

	/* Write out the address of the gatt table */
	OUTREG32(amd_irongate_private.registers, AMD_ATTBASE,
		 agp_bridge.gatt_bus_addr);

	/* Write the Sync register */
	pci_write_config_byte(agp_bridge.dev, AMD_MODECNTL, 0x80);

	/* Write the enable register */
	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg | 0x0004);
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write out the size register */
	pci_read_config_dword(agp_bridge.dev, AMD_APSIZE, &temp);
	temp = (((temp & ~(0x0000000e)) | current_size->size_value)
		| 0x00000001);
	pci_write_config_dword(agp_bridge.dev, AMD_APSIZE, temp);

	/* Flush the tlb */
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);

	/* Get the address for the gart region */
	pci_read_config_dword(agp_bridge.dev, AMD_APBASE, &temp);
	addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
#ifdef __alpha__
	/* ??? Presumably what is wanted is the bus address as seen
	   from the CPU side, since it appears that this value is
	   exported to userland via an ioctl.  The terminology below
	   is confused, mixing `physical address' with `bus address',
	   as x86 folk are wont to do.  */
	addr = virt_to_phys(ioremap(addr, 0));
#endif
	agp_bridge.gart_bus_addr = addr;
	return 0;
}

static void amd_irongate_cleanup(void)
{
	aper_size_info_32 *previous_size;
	u32 temp;
	u16 enable_reg;

	previous_size = A_SIZE_32(agp_bridge.previous_size);

	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg & ~(0x0004));
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write back the previous size and disable gart translation */
	pci_read_config_dword(agp_bridge.dev, AMD_APSIZE, &temp);
	temp = ((temp & ~(0x0000000f)) | previous_size->size_value);
	pci_write_config_dword(agp_bridge.dev, AMD_APSIZE, temp);
	iounmap((void *) amd_irongate_private.registers);
}

/*
 * This routine could be implemented by taking the addresses
 * written to the GATT, and flushing them individually.  However
 * currently it just flushes the whole table.  Which is probably
 * more efficent, since agp_memory blocks can be a large number of
 * entries.
 */

static void amd_irongate_tlbflush(agp_memory * temp)
{
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);
}

static unsigned long amd_irongate_mask_memory(unsigned long addr, int type)
{
	/* Only type 0 is supported by the irongate */

	return addr | agp_bridge.masks[0].mask;
}

static aper_size_info_32 amd_irongate_sizes[7] =
{
	{2048, 524288, 9, 0x0000000c},
	{1024, 262144, 8, 0x0000000a},
	{512, 131072, 7, 0x00000008},
	{256, 65536, 6, 0x00000006},
	{128, 32768, 5, 0x00000004},
	{64, 16384, 4, 0x00000002},
	{32, 8192, 3, 0x00000000}
};

static gatt_mask amd_irongate_masks[] =
{
	{0x00000001, 0}
};

static void amd_irongate_setup(void)
{
	agp_bridge.masks = amd_irongate_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) amd_irongate_sizes;
	agp_bridge.size_type = U32_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = (void *) &amd_irongate_private;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = amd_irongate_configure;
	agp_bridge.fetch_size = amd_irongate_fetch_size;
	agp_bridge.cleanup = amd_irongate_cleanup;
	agp_bridge.tlb_flush = amd_irongate_tlbflush;
	agp_bridge.mask_memory = amd_irongate_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
}

#endif

#ifdef CONFIG_AGP_ALI

static int ali_fetch_size(void)
{
	int i;
	u32 temp;
	aper_size_info_32 *values;

	pci_read_config_dword(agp_bridge.dev, ALI_ATTBASE, &temp);
	temp &= ~(0xfffffff0);
	values = A_SIZE_32(agp_bridge.aperture_sizes);

	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void ali_tlbflush(agp_memory * mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL,
			       ((temp & 0xffffff00) | 0x00000090));
	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL,
			       ((temp & 0xffffff00) | 0x00000010));
}

static void ali_cleanup(void)
{
	aper_size_info_32 *previous_size;
	u32 temp;

	previous_size = A_SIZE_32(agp_bridge.previous_size);

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL,
			       ((temp & 0xffffff00) | 0x00000090));
	pci_write_config_dword(agp_bridge.dev, ALI_ATTBASE,
			       previous_size->size_value);
}

static int ali_configure(void)
{
	u32 temp;
	aper_size_info_32 *current_size;

	current_size = A_SIZE_32(agp_bridge.current_size);

	/* aperture size and gatt addr */
	pci_write_config_dword(agp_bridge.dev, ALI_ATTBASE,
		    agp_bridge.gatt_bus_addr | current_size->size_value);

	/* tlb control */
	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL,
			       ((temp & 0xffffff00) | 0x00000010));

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, ALI_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	return 0;
}

static unsigned long ali_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}


/* Setup function */
static gatt_mask ali_generic_masks[] =
{
	{0x00000000, 0}
};

static aper_size_info_32 ali_generic_sizes[7] =
{
	{256, 65536, 6, 10},
	{128, 32768, 5, 9},
	{64, 16384, 4, 8},
	{32, 8192, 3, 7},
	{16, 4096, 2, 6},
	{8, 2048, 1, 4},
	{4, 1024, 0, 3}
};

static void ali_generic_setup(void)
{
	agp_bridge.masks = ali_generic_masks;
	agp_bridge.num_of_masks = 1;
	agp_bridge.aperture_sizes = (void *) ali_generic_sizes;
	agp_bridge.size_type = U32_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = ali_configure;
	agp_bridge.fetch_size = ali_fetch_size;
	agp_bridge.cleanup = ali_cleanup;
	agp_bridge.tlb_flush = ali_tlbflush;
	agp_bridge.mask_memory = ali_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
}

#endif



/* Supported Device Scanning routine */

static void agp_find_supported_device(void)
{
	struct pci_dev *dev = NULL;
	u8 cap_ptr = 0x00;
	u32 cap_id, scratch;

	if ((dev = pci_find_class(PCI_CLASS_BRIDGE_HOST << 8, NULL)) == NULL) {
		agp_bridge.type = NOT_SUPPORTED;
		return;
	}
	agp_bridge.dev = dev;

	/* Need to test for I810 here */
#ifdef CONFIG_AGP_I810
	if (dev->vendor == PCI_VENDOR_ID_INTEL) {
		struct pci_dev *i810_dev;

		switch (dev->device) {
		case PCI_DEVICE_ID_INTEL_810_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					       PCI_DEVICE_ID_INTEL_810_1,
						   NULL);
			if (i810_dev == NULL) {
				printk("agpgart: Detected an Intel i810,"
				       " but could not find the secondary"
				       " device.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
			printk(KERN_INFO "agpgart: Detected an Intel "
			       "i810 Chipset.\n");
			agp_bridge.type = INTEL_I810;
			agp_bridge.intel_i810_setup(i810_dev);
			return;

		case PCI_DEVICE_ID_INTEL_810_DC100_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					 PCI_DEVICE_ID_INTEL_810_DC100_1,
						   NULL);
			if (i810_dev == NULL) {
				printk("agpgart: Detected an Intel i810 "
				       "DC100, but could not find the "
				       "secondary device.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
			printk(KERN_INFO "agpgart: Detected an Intel i810 "
			       "DC100 Chipset.\n");
			agp_bridge.type = INTEL_I810;
			agp_bridge.intel_i810_setup(i810_dev);
			return;

		case PCI_DEVICE_ID_INTEL_810_E_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					     PCI_DEVICE_ID_INTEL_810_E_1,
						   NULL);
			if (i810_dev == NULL) {
				printk("agpgart: Detected an Intel i810 E"
				    ", but could not find the secondary "
				       "device.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
			printk(KERN_INFO "agpgart: Detected an Intel i810 E "
			       "Chipset.\n");
			agp_bridge.type = INTEL_I810;
			agp_bridge.intel_i810_setup(i810_dev);
			return;
		default:
			break;
		}
	}
#endif
	/* find capndx */
	pci_read_config_dword(dev, 0x04, &scratch);

	if (!(scratch & 0x00100000)) {
		agp_bridge.type = NOT_SUPPORTED;
		return;
	}
	pci_read_config_byte(dev, 0x34, &cap_ptr);

	if (cap_ptr != 0x00) {
		do {
			pci_read_config_dword(dev, cap_ptr, &cap_id);

			if ((cap_id & 0xff) != 0x02)
				cap_ptr = (cap_id >> 8) & 0xff;
		}
		while (((cap_id & 0xff) != 0x02) && (cap_ptr != 0x00));
	}
	if (cap_ptr == 0x00) {
		agp_bridge.type = NOT_SUPPORTED;
		return;
	}
	agp_bridge.capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(agp_bridge.dev,
			      agp_bridge.capndx + 4,
			      &agp_bridge.mode);

	switch (dev->vendor) {
#ifdef CONFIG_AGP_INTEL
	case PCI_VENDOR_ID_INTEL:
		switch (dev->device) {
		case PCI_DEVICE_ID_INTEL_82443LX_0:
			agp_bridge.type = INTEL_LX;
			printk(KERN_INFO "agpgart: Detected an Intel 440LX"
			       " Chipset.\n");
			agp_bridge.intel_generic_setup();
			return;

		case PCI_DEVICE_ID_INTEL_82443BX_0:
			agp_bridge.type = INTEL_BX;
			printk(KERN_INFO "agpgart: Detected an Intel 440BX "
			       "Chipset.\n");
			agp_bridge.intel_generic_setup();
			return;

		case PCI_DEVICE_ID_INTEL_82443GX_0:
			agp_bridge.type = INTEL_GX;
			printk(KERN_INFO "agpgart: Detected an Intel 440GX "
			       "Chipset.\n");
			agp_bridge.intel_generic_setup();
			return;

		default:
			if (agp_try_unsupported != 0) {
				printk("agpgart: Trying generic intel "
				       "routines for device id: %x\n",
				       dev->device);
				agp_bridge.type = INTEL_GENERIC;
				agp_bridge.intel_generic_setup();
				return;
			} else {
				printk("agpgart: Unsupported intel chipset,"
				       " you might want to try "
				       "agp_try_unsupported=1.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
		}
		break;
#endif

#ifdef CONFIG_AGP_VIA
	case PCI_VENDOR_ID_VIA:
		switch (dev->device) {
		case PCI_DEVICE_ID_VIA_82C597_0:
			agp_bridge.type = VIA_VP3;
			printk(KERN_INFO "agpgart: Detected a VIA VP3 "
			       "Chipset.\n");
			agp_bridge.via_generic_setup();
			return;

		case PCI_DEVICE_ID_VIA_82C598_0:
			agp_bridge.type = VIA_MVP3;
			printk(KERN_INFO "agpgart: Detected a VIA MVP3 "
			       "Chipset.\n");
			agp_bridge.via_generic_setup();
			return;

		case PCI_DEVICE_ID_VIA_82C691_0:
			agp_bridge.type = VIA_APOLLO_PRO;
			printk(KERN_INFO "agpgart: Detected a VIA Apollo "
			       "Pro Chipset.\n");
			agp_bridge.via_generic_setup();
			return;

		default:
			if (agp_try_unsupported != 0) {
				printk("agpgart: Trying generic VIA routines"
				    " for device id: %x\n", dev->device);
				agp_bridge.type = VIA_GENERIC;
				agp_bridge.via_generic_setup();
				return;
			} else {
				printk("agpgart: Unsupported VIA chipset,"
				       " you might want to try "
				       "agp_try_unsupported=1.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
		}
		break;
#endif

#ifdef CONFIG_AGP_SIS
	case PCI_VENDOR_ID_SI:
		switch (dev->device) {
			/* ToDo need to find out the
			 * specific devices supported.
			 */
		default:
			if (agp_try_unsupported != 0) {
				printk("agpgart: Trying generic SiS routines"
				    " for device id: %x\n", dev->device);
				agp_bridge.type = SIS_GENERIC;
				agp_bridge.sis_generic_setup();
				return;
			} else {
				printk("agpgart: Unsupported SiS chipset, "
				       "you might want to try "
				       "agp_try_unsupported=1.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
		}
		break;
#endif

#ifdef CONFIG_AGP_AMD
	case PCI_VENDOR_ID_AMD:
		switch (dev->device) {
		case PCI_DEVICE_ID_AMD_IRONGATE_0:
			agp_bridge.type = AMD_IRONGATE;
			printk(KERN_INFO "agpgart: Detected an AMD Irongate"
			       " Chipset.\n");
			agp_bridge.amd_irongate_setup();
			return;

		default:
			if (agp_try_unsupported != 0) {
				printk("agpgart: Trying Amd irongate"
				       " routines for device id: %x\n",
				       dev->device);
				agp_bridge.type = AMD_GENERIC;
				agp_bridge.amd_irongate_setup();
				return;
			} else {
				printk("agpgart: Unsupported Amd chipset,"
				       " you might want to try "
				       "agp_try_unsupported=1.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
		}
		break;
#endif

#ifdef CONFIG_AGP_ALI
	case PCI_VENDOR_ID_AL:
		switch (dev->device) {
		case PCI_DEVICE_ID_AL_M1541_0:
			agp_bridge.type = ALI_M1541;
			printk(KERN_INFO "agpgart: Detected an ALi M1541"
			       " Chipset\n");
			agp_bridge.ali_generic_setup();
			return;
		default:
			if (agp_try_unsupported != 0) {
				printk("agpgart: Trying ALi generic routines"
				    " for device id: %x\n", dev->device);
				agp_bridge.type = ALI_GENERIC;
				agp_bridge.ali_generic_setup();
				return;
			} else {
				printk("agpgart: Unsupported ALi chipset,"
				       " you might want to type "
				       "agp_try_unsupported=1.\n");
				agp_bridge.type = NOT_SUPPORTED;
				return;
			}
		}
		break;
#endif
	default:
		agp_bridge.type = NOT_SUPPORTED;
		return;
	}
}

struct agp_max_table {
	int mem;
	int agp;
};

static struct agp_max_table maxes_table[9] =
{
	{0, 0},
	{32, 4},
	{64, 28},
	{128, 96},
	{256, 204},
	{512, 440},
	{1024, 942},
	{2048, 1920},
	{4096, 3932}
};

static int agp_find_max(void)
{
	long memory, t, index, result;

	memory = virt_to_phys(high_memory) >> 20;
	index = 1;

	while ((memory > maxes_table[index].mem) &&
	       (index < 8)) {
		index++;
	}

	t = (memory - maxes_table[index - 1].mem) /
	    (maxes_table[index].mem - maxes_table[index - 1].mem);

	result = maxes_table[index - 1].agp +
	    (t * (maxes_table[index].agp - maxes_table[index - 1].agp));

	printk(KERN_INFO "agpgart: Maximum main memory to use "
	       "for agp memory: %ldM\n", result);
	result = result << (20 - PAGE_SHIFT);
	return result;
}

#define AGPGART_VERSION_MAJOR 0
#define AGPGART_VERSION_MINOR 99

static agp_version agp_current_version =
{
	AGPGART_VERSION_MAJOR,
	AGPGART_VERSION_MINOR
};

static int agp_backend_initialize(void)
{
	int size_value;

	memset(&agp_bridge, 0, sizeof(struct agp_bridge_data));
	agp_bridge.type = NOT_SUPPORTED;
#ifdef CONFIG_AGP_INTEL
	agp_bridge.intel_generic_setup = intel_generic_setup;
#endif
#ifdef CONFIG_AGP_I810
	agp_bridge.intel_i810_setup = intel_i810_setup;
#endif
#ifdef CONFIG_AGP_VIA
	agp_bridge.via_generic_setup = via_generic_setup;
#endif
#ifdef CONFIG_AGP_SIS
	agp_bridge.sis_generic_setup = sis_generic_setup;
#endif
#ifdef CONFIG_AGP_AMD
	agp_bridge.amd_irongate_setup = amd_irongate_setup;
#endif
#ifdef CONFIG_AGP_ALI
	agp_bridge.ali_generic_setup = ali_generic_setup;
#endif
	agp_bridge.max_memory_agp = agp_find_max();
	agp_bridge.version = &agp_current_version;
	agp_find_supported_device();

	if (agp_bridge.needs_scratch_page == TRUE) {
		agp_bridge.scratch_page = agp_alloc_page();

		if (agp_bridge.scratch_page == 0) {
			printk("agpgart: unable to get memory for "
			       "scratch page.\n");
			return -ENOMEM;
		}
		agp_bridge.scratch_page =
		    virt_to_phys((void *) agp_bridge.scratch_page);
		agp_bridge.scratch_page =
		    agp_bridge.mask_memory(agp_bridge.scratch_page, 0);
	}
	if (agp_bridge.type == NOT_SUPPORTED) {
		printk("agpgart: no supported devices found.\n");
		return -EINVAL;
	}
	size_value = agp_bridge.fetch_size();

	if (size_value == 0) {
		printk("agpgart: unable to detrimine aperture size.\n");
		return -EINVAL;
	}
	if (agp_bridge.create_gatt_table()) {
		printk("agpgart: unable to get memory for graphics "
		       "translation table.\n");
		return -ENOMEM;
	}
	agp_bridge.key_list = vmalloc(PAGE_SIZE * 4);

	if (agp_bridge.key_list == NULL) {
		printk("agpgart: error allocating memory for key lists.\n");
		agp_bridge.free_gatt_table();
		return -ENOMEM;
	}
	memset(agp_bridge.key_list, 0, PAGE_SIZE * 4);

	if (agp_bridge.configure()) {
		printk("agpgart: error configuring host chipset.\n");
		agp_bridge.free_gatt_table();
		vfree(agp_bridge.key_list);
		return -EINVAL;
	}
	printk(KERN_INFO "agpgart: Physical address of the agp aperture:"
	       " 0x%lx\n", agp_bridge.gart_bus_addr);
	printk(KERN_INFO "agpgart: Agp aperture is %dM in size.\n",
	       size_value);
	return 0;
}

static void agp_backend_cleanup(void)
{
	agp_bridge.cleanup();
	agp_bridge.free_gatt_table();
	vfree(agp_bridge.key_list);

	if (agp_bridge.needs_scratch_page == TRUE) {
		agp_bridge.scratch_page &= ~(0x00000fff);
		agp_destroy_page((unsigned long)
				 phys_to_virt(agp_bridge.scratch_page));
	}
}

extern int agp_frontend_initialize(void);
extern void agp_frontend_cleanup(void);

static int __init agp_init(void)
{
	int ret_val;

	printk(KERN_INFO "Linux agpgart interface v%d.%d (c) Jeff Hartmann\n",
	       AGPGART_VERSION_MAJOR, AGPGART_VERSION_MINOR);
	ret_val = agp_backend_initialize();

	if (ret_val != 0) {
		return ret_val;
	}
	ret_val = agp_frontend_initialize();

	if (ret_val != 0) {
		agp_backend_cleanup();
		return ret_val;
	}
	return 0;
}

static void __exit agp_cleanup(void)
{
	agp_frontend_cleanup();
	agp_backend_cleanup();
}

module_init(agp_init);
module_exit(agp_cleanup);