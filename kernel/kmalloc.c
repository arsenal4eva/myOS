#include "kernel/kmalloc.h"
#include "kernel/pmm.h"
#include "kernel/paging.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"


#define HEAP_START 0xD0000000u
#define HEAP_MAX   (32u * 1024u * 1024u)

typedef struct block {
    size_t size;
    int free;
    struct block *next;
} block_t;

static uint32_t heap_brk = HEAP_START;
static block_t *head = 0;

static void *sbrk_pages(size_t n) {
    if (heap_brk + n * 0x1000 > HEAP_START + HEAP_MAX) return 0;
    uint32_t base = heap_brk;
    for (size_t i = 0; i < n; i++) {
        uint32_t f = pmm_alloc_frame();
        if (!f) return 0;
        paging_map(heap_brk, f, 1, 0);
        memset((void *)heap_brk, 0, 0x1000);
        heap_brk += 0x1000;
    }
    return (void *)base;
}

void heap_init(void) {
    head = 0; heap_brk = HEAP_START;
    kprintf("[heap] snack-allocator ready at 0x%x\n", HEAP_START);
}

void *kmalloc(size_t size) {
    if (!size) return 0;
    size = (size + 7) & ~7u;
    for (block_t *b = head; b; b = b->next) {
        if (b->free && b->size >= size) { b->free = 0; return (void *)(b + 1); }
    }
    size_t total = size + sizeof(block_t);
    size_t pages = (total + 0xFFF) / 0x1000;
    block_t *b = (block_t *)sbrk_pages(pages);
    if (!b) return 0;
    b->size = pages * 0x1000 - sizeof(block_t);
    b->free = 0; b->next = 0;
    if (!head) head = b;
    else { block_t *t = head; while (t->next) t = t->next; t->next = b; }
    return (void *)(b + 1);
}

void kfree(void *p) {
    if (!p) return;
    block_t *b = ((block_t *)p) - 1;
    b->free = 1;
    for (block_t *c = head; c && c->next;) {
        if (c->free && c->next->free) c->next = c->next->next;
        else c = c->next;
    }
}
