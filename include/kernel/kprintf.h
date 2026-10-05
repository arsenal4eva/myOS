#ifndef _KERNEL_KPRINTF_H
#define _KERNEL_KPRINTF_H
#include <stdarg.h>
#include <stddef.h>
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);
void kprintf_begin_capture(char *buf, size_t cap);
size_t kprintf_end_capture(void);
int kprintf_capturing(void);
#endif
