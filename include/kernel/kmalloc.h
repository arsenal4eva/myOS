#ifndef _KERNEL_KMALLOC_H
#define _KERNEL_KMALLOC_H
#include <stddef.h>
void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *p);
#endif
