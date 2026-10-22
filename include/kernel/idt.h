#ifndef _KERNEL_IDT_H
#define _KERNEL_IDT_H
#include <stdint.h>
typedef struct regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} regs_t;
void idt_init(void);
void idt_flush(uint32_t ptr);
void idt_set_gate(uint8_t n, uint32_t base, uint16_t sel, uint8_t flags);
void isr_handler(regs_t *r);
void irq_handler(regs_t *r);
typedef void (*isr_t)(regs_t *);
void irq_install_handler(int irq, isr_t h);
void irq_uninstall_handler(int irq);
#endif
