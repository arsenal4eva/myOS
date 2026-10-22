#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"

#define P_PRESENT 0x1
#define P_RW      0x2
#define P_USER    0x4

static uint32_t *page_dir = 0;

static void flush_tlb(uint32_t v) { __asm__ volatile("invlpg (%0)" : : "r"(v) : "memory"); }

void paging_init(void) {
    uint32_t pd_phys = pmm_alloc_frame();
    uint32_t pt_phys = pmm_alloc_frame();
    page_dir = (uint32_t *)pd_phys; 
    uint32_t *pt = (uint32_t *)pt_phys;
    memset(page_dir, 0, 4096);
    for (int i = 0; i < 1024; i++) pt[i] = (i * 0x1000) | (P_PRESENT | P_RW);
    page_dir[0] = pt_phys | (P_PRESENT | P_RW);
    page_dir[768] = pt_phys | (P_PRESENT | P_RW);
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
    uint32_t cr0; __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    kprintf("[paging] enabled: PD=0x%x PT=0x%x (identity + 0xC0000000)\n", pd_phys, pt_phys);
}

void paging_map(uint32_t virt, uint32_t phys, int writable, int user) {
    uint32_t pd = virt >> 22, pt = (virt >> 12) & 0x3FF;
    uint32_t *pde = &page_dir[pd];
    uint32_t *table;
    if (!(*pde & P_PRESENT)) {
        uint32_t t = pmm_alloc_frame();
        *pde = t | P_PRESENT | P_RW | (user ? P_USER : 0);
        table = (uint32_t *)t;
        memset(table, 0, 4096);
    } else {
        table = (uint32_t *)(*pde & ~0xFFFu);
    }
    table[pt] = (phys & ~0xFFFu) | P_PRESENT | (writable ? P_RW : 0) | (user ? P_USER : 0);
    flush_tlb(virt);
}
void paging_unmap(uint32_t virt) {
    uint32_t pd = virt >> 22, pt = (virt >> 12) & 0x3FF;
    if (page_dir[pd] & P_PRESENT) {
        uint32_t *table = (uint32_t *)(page_dir[pd] & ~0xFFFu);
        table[pt] = 0;
        flush_tlb(virt);
    }
}


