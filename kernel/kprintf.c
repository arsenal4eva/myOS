#include "kernel/kprintf.h"
#include "kernel/tty.h"
#include "kernel/serial.h"
#include <stddef.h>

static char *cap_buf;
static size_t cap_pos, cap_size;

void kprintf_begin_capture(char *buf, size_t cap) {
    cap_buf = buf; cap_pos = 0; cap_size = cap;
}
size_t kprintf_end_capture(void) {
    size_t n = cap_pos;
    cap_buf = 0; cap_pos = cap_size = 0;
    return n;
}
int kprintf_capturing(void) { return cap_buf != 0; }

static void out(char c) {
    if (cap_buf) {
        if (cap_pos + 1 < cap_size)
            cap_buf[cap_pos++] = c;
        return;
    }
    tty_putchar(c); serial_putchar(c);
}
static void outs(const char *s) { while (*s) out(*s++); }

static void out_uint(unsigned long v, int base, int upper) {
    char buf[32]; int i = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) buf[i++] = '0';
    while (v) { buf[i++] = dig[v % (unsigned)base]; v /= (unsigned)base; }
    while (i--) out(buf[i]);
}

void kvprintf(const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out(*fmt); continue; }
        fmt++;
        switch (*fmt) {
        case '%': out('%'); break;
        case 'c': out((char)va_arg(ap, int)); break;
        case 's': { const char *s = va_arg(ap, const char *); outs(s ? s : "(null)"); break; }
        case 'd': case 'i': {
            long v = va_arg(ap, int);
            if (v < 0) { out('-'); v = -v; }
            out_uint((unsigned long)v, 10, 0); break; }
        case 'u': out_uint(va_arg(ap, unsigned int), 10, 0); break;
        case 'x': out_uint(va_arg(ap, unsigned int), 16, 0); break;
        case 'X': out_uint(va_arg(ap, unsigned int), 16, 1); break;
        case 'p': outs("0x"); out_uint((unsigned long)va_arg(ap, void *), 16, 0); break;
        default: out('%'); out(*fmt); break;
        }
    }
}
void kprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); kvprintf(fmt, ap); va_end(ap);
}
