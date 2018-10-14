// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <linux/types.h>
#include <asm/sysreg.h>
#include <asm/tlbflush.h>

#define l0_index(ia) ((ia & 0xFF8000000000) >> 39)
#define l1_index(ia) ((ia & 0x7FC0000000) >> 30)
#define l2_index(ia) ((ia & 0x3FE00000) >> 21)
#define l3_index(ia) ((ia & 0x1FF000) >> 12)
#define make_rwx(pte) (*pte = *pte & ~0x00600000000000C0)

u64 *get_mmu_desc(int index, u64 *page)
{
	return &page[index];
}

u64 *get_mmu_page(int index, u64 *page, u64 *freemem, bool *block)
{
	u64 *desc = get_mmu_desc(index, page);

	if ((*desc & 0x1) && !(*desc & 0x2)) {
		/* Hit a block descriptor */
		*block = true;
		return desc;
	}
	/*
	 * All the physical addresses should already be mapped, so this should
	 * only be triggering for VAs.  The MMU gets turned off just before
	 * entering the proper Linux kernel, so these mappings are quick and
	 * dirty just to get UEFI settled.  The kernel is expected to blow this
	 * all away when it builds its own MMU tables, so we don't need to be
	 * worried about cleanup.
	 */
	if (!(*desc & 0x3)) {
		memset((void *)*freemem, 0, SZ_4K);
		*desc = *freemem | 0x3;
		*freemem += SZ_4K;
		dsb(sy);
		asm volatile("dc civac, %0" : : "r" (desc) : "memory");
		flush_tlb_all();
	}
	return (u64 *)(*desc & 0xFFFFFFFFF000);
}

u64 *get_pte(u64 addr, u64 *freemem)
{
	u64 tcr = read_sysreg(tcr_el1);
	u64 *ttbr = (u64 *)read_sysreg(ttbr0_el1);
	u64 *pte;
	u64 *page;
	bool four_level_translation = (64 - (tcr & 0x1F)) < 39 ? false : true;
	bool block = false;

	if (four_level_translation)
		page = get_mmu_page(l0_index(addr), ttbr, freemem, &block);
	else
		page = ttbr;

	page = get_mmu_page(l1_index(addr), page, freemem, &block);
	if (block)
		return page;
	page = get_mmu_page(l2_index(addr), page, freemem, &block);
	if (block)
		return page;
	pte = get_mmu_desc(l3_index(addr), page);

	return pte;
}

void map_and_set_permissions(u64 paddr, u64 vaddr, u64 pagecount, u64 *freemem)
{
	u64 *pa_pte;
	u64 *va_pte;
	int i;

	for (i = 0; i < pagecount; i++, paddr += SZ_4K, vaddr += SZ_4K) {
		pa_pte = get_pte(paddr, freemem);
		va_pte = get_pte(vaddr, freemem);
		make_rwx(pa_pte);
		/* set access flag to prevent AF fault if UEFI accesses this */
		*va_pte = paddr | 0x403;
		dsb(sy);
		asm volatile("dc civac, %0" : : "r" (pa_pte) : "memory");
		asm volatile("dc civac, %0" : : "r" (va_pte) : "memory");
	}

	flush_tlb_all();
}

/*
 * UEFI uses a 4k page, with 1:1 mappings and does not support the split
 * address space so we only need to be concerned about ttbr0.
 */
void do_sdm850_uefi_workaround(efi_memory_desc_t *memmap,
			       efi_memory_desc_t *vamap,
			       unsigned long map_size,
			       unsigned long desc_size,
			       int vamapcount)
{
	int i = map_size / desc_size;
	u64 freemem;

	/*
	 * look for an unused memory range that can support the max num of mmu
	 * pages we may need to create (512 x 512 + 1).  If we can't find one,
	 * we are screwed.  We should be all but guaranteed to find one.  We
	 * don't know exactally where UEFI put the existing mmu pages, so
	 * reclaiming memory is risky.
	 */
	while (1) {
		if (memmap->type == EFI_CONVENTIONAL_MEMORY &&
		    memmap->num_pages > 0x40000)
			break;
		memmap = (void *)memmap + desc_size;
		i--;
		if (!i)
			return;
	}

	freemem = memmap->phys_addr;

	for (i = 0; i < vamapcount; i++, vamap = (void *)vamap + desc_size) {
		if (!(vamap->attribute & EFI_MEMORY_RUNTIME))
			continue;
		map_and_set_permissions(vamap->phys_addr, vamap->virt_addr,
					vamap->num_pages, &freemem);
	}
}
