#include "kernel/idt.h"
#include "kernel/port.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"

struct idt_entry { uint16_t base_lo; uint16_t sel; uint8_t always0; uint8_t flags; uint16_t base_hi; } __attribute__((packed));
struct idt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr ip;
extern void idt_flush(uint32_t);

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void);extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void);extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void);extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void);extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void);extern void isr30(void); extern void isr31(void);
extern void irq0(void);  extern void irq1(void); extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void); extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void); extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void);extern void irq14(void); extern void irq15(void);

static isr_t irq_routines[16] = {0};

static const char *exc_msg[] = {
    "Division By Zero","Debug","NMI","Breakpoint","Overflow","Bound Range",
    "Invalid Opcode","No Coprocessor","Double Fault","Coprocessor Overrun",
    "Invalid TSS","Segment Not Present","Stack Fault","General Protection",
    "Page Fault","Reserved","x87 FPU","Alignment Check","Machine Check",
    "SIMD Exception","Virtualization","Control Protection","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved","Hypervisor Injection","VMM Comm",
    "Security Exception","Reserved"
};

static void set_gate(uint8_t n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].base_lo = base & 0xFFFF; idt[n].base_hi = (base >> 16) & 0xFFFF;
    idt[n].sel = sel; idt[n].always0 = 0; idt[n].flags = flags;
}

static void pic_remap(void) {
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0x00); io_wait(); outb(0xA1, 0x00); io_wait(); 
}

void idt_init(void) {
    memset(&idt, 0, sizeof(idt));
    void (*isrs[32])(void) = {isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,
        isr10,isr11,isr12,isr13,isr14,isr15,isr16,isr17,isr18,isr19,isr20,isr21,
        isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31};
    for (int i = 0; i < 32; i++) set_gate(i, (uint32_t)isrs[i], 0x08, 0x8E);
    void (*irqs[16])(void) = {irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,
        irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15};
    pic_remap();
    for (int i = 0; i < 16; i++) set_gate(32+i, (uint32_t)irqs[i], 0x08, 0x8E);
    ip.limit = sizeof(idt)-1; ip.base = (uint32_t)&idt;
    idt_flush((uint32_t)&ip);
}

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

void irq_install_handler(int irq, isr_t h) { if (irq >= 0 && irq < 16) irq_routines[irq] = h; }
void irq_uninstall_handler(int irq) { if (irq >= 0 && irq < 16) irq_routines[irq] = 0; }
