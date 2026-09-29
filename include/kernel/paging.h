#ifndef _KERNEL_PAGING_H
#define _KERNEL_PAGING_H
#include <stdint.h>
void paging_init(void);
void paging_map(uint32_t virt, uint32_t phys, int writable, int user);
void paging_unmap(uint32_t virt);
#endif
