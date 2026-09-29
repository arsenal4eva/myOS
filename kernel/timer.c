#include "kernel/timer.h"
#include "kernel/idt.h"
#include "kernel/port.h"

static volatile uint32_t ticks = 0;
static void on_tick(regs_t *r) { (void)r; ticks++; }

void timer_init(uint32_t freq_hz) {
    irq_install_handler(0, on_tick);
    uint32_t div = 1193180 / freq_hz;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF); outb(0x40, (div >> 8) & 0xFF);
}
uint32_t timer_ticks(void) { return ticks; }
void timer_sleep(uint32_t t) { uint32_t start = ticks; while (ticks - start < t) __asm__ volatile("hlt"); }
