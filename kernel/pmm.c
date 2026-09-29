#include "kernel/pmm.h"
#include "kernel/kprintf.h"

#define MAX_FRAMES (1024*1024) 
#define BITMAP_WORDS (MAX_FRAMES / 32)

static uint32_t bitmap[BITMAP_WORDS];
static uint32_t total_frames = 0, free_frames = 0;
static uint32_t mem_base = 0;

static void set_used(uint32_t f) { bitmap[f/32] |= (1u << (f%32)); }
static void set_free(uint32_t f) { bitmap[f/32] &= ~(1u << (f%32)); }
static int is_free(uint32_t f) { return !(bitmap[f/32] & (1u << (f%32))); }

extern uint32_t _kernel_end;

void pmm_init(multiboot_info_t *mbi, uint32_t kernel_end) {
    for (uint32_t i = 0; i < BITMAP_WORDS; i++) bitmap[i] = 0xFFFFFFFF; 
    total_frames = 0; free_frames = 0;
    uint32_t mem_top = 0;

    if (mbi->flags & (1 << 6)) {
        uint32_t addr = mbi->mmap_addr;
        uint32_t end = addr + mbi->mmap_length;
        while (addr < end) {
            uint32_t *raw = (uint32_t *)addr;
            uint32_t esize = raw[0];
            uint32_t base_lo = raw[1], base_hi = raw[2];
            uint32_t len_lo = raw[3], len_hi = raw[4];
            uint32_t type = raw[5];
            if (type == 1 && base_hi == 0 && len_hi == 0) {
                uint32_t base = base_lo, len = len_lo;
                uint32_t start = (uint32_t)((base + 0xFFF) & ~0xFFFu);
                uint32_t stop = (uint32_t)((base + len) & ~0xFFFu);
                if (stop > mem_top) mem_top = stop;
                for (uint32_t a = start; a + 0x1000 <= stop && a + 0x1000 > a; a += 0x1000) {
                    uint32_t f = a >> 12;
                    if (f < MAX_FRAMES && !is_free(f)) {
                        set_free(f); total_frames++; free_frames++;
                    }
                }
            }
            addr += esize + sizeof(esize);
        }
    } else {
        mem_top = 0x100000 + mbi->mem_upper * 1024;
        for (uint32_t a = 0x100000; a < mem_top; a += 0x1000) {
            uint32_t f = a >> 12;
            if (f < MAX_FRAMES && !is_free(f)) { set_free(f); total_frames++; free_frames++; }
        }
    }

    for (uint32_t a = 0; a < kernel_end; a += 0x1000) {
        uint32_t f = a >> 12;
        if (f < MAX_FRAMES && is_free(f)) { set_used(f); free_frames--; }
    }
    mem_base = 0;
    kprintf("[pmm] frames total=%d free=%d mem_top=0x%x kend=0x%x\n",
            total_frames, free_frames, mem_top, kernel_end);
}

uint32_t pmm_alloc_frame(void) {
    for (uint32_t f = 0; f < MAX_FRAMES; f++) {
        if (is_free(f)) { set_used(f); free_frames--; return f << 12; }
    }
    return 0;
}
void pmm_free_frame(uint32_t phys) {
    uint32_t f = phys >> 12;
    if (f < MAX_FRAMES && !is_free(f)) { set_free(f); free_frames++; }
}
uint32_t pmm_free_count(void) { return free_frames; }
uint32_t pmm_total_count(void) { return total_frames; }
