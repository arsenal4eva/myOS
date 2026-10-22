#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H
#include <stdint.h>
#include "multiboot.h"
void pmm_init(multiboot_info_t *mbi, uint32_t kernel_end);
uint32_t pmm_alloc_frame(void); 
void pmm_free_frame(uint32_t phys);
uint32_t pmm_free_count(void);
uint32_t pmm_total_count(void);
#endif
