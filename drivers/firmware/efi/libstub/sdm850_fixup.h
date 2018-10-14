/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _DRIVERS_FIRMWARE_EFI_SDM850_FIXUP_H
#define _DRIVERS_FIRMWARE_EFI_SDM850_FIXUP_H

#include <linux/efi.h>

void do_sdm850_uefi_workaround(efi_memory_desc_t *memmap,
			       efi_memory_desc_t *vamap,
			       unsigned long map_size,
			       unsigned long desc_size,
			       int vamapcount);

#endif
