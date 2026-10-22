#include "kernel/idt.h"
#include "kernel/port.h"
#include "kernel/kprintf.h"

/* Tables, gates, PIC remap, and flush now live in kernel/arch/i386/idt.s.
 * This file keeps only the high-level C fault/IRQ dispatch. */
extern isr_t irq_routines[16];

static const char *exc_msg[] = {
    "Division By Zero","Debug","NMI","Breakpoint","Overflow","Bound Range",
    "Invalid Opcode","No Coprocessor","Double Fault","Coprocessor Overrun",
    "Invalid TSS","Segment Not Present","Stack Fault","General Protection",
    "Page Fault","Reserved","x87 FPU","Alignment Check","Machine Check",
    "SIMD Exception","Virtualization","Control Protection","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved","Hypervisor Injection","VMM Comm",
    "Security Exception","Reserved"
};

void isr_handler(regs_t *r) {
    if (r->int_no == 14) {
        uint32_t cr2; __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("\n[PANIC] Page Fault at 0x%x (eip=0x%x err=0x%x)\n", cr2, r->eip, r->err_code);
    } else {
        const char *m = r->int_no < 32 ? exc_msg[r->int_no] : "Unknown";
        kprintf("\n[PANIC] Exception %d (%s) eip=0x%x err=0x%x\n", r->int_no, m, r->eip, r->err_code);
    }
    __asm__ volatile("cli; hlt");
}

void irq_handler(regs_t *r) {
    int irq = (int)r->int_no - 32;
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
    if (irq >= 0 && irq < 16 && irq_routines[irq]) irq_routines[irq](r);
}
