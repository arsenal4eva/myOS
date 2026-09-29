#include "kernel/gdt.h"
#include "kernel/port.h"
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_lo; uint16_t base_lo; uint8_t base_mid;
    uint8_t access; uint8_t gran; uint8_t base_hi;
} __attribute__((packed));
struct gdt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr gp;

extern void gdt_flush(uint32_t);

static void set_gate(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_lo = base & 0xFFFF; gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].base_hi = (base >> 24) & 0xFF;
    gdt[i].limit_lo = limit & 0xFFFF;
    gdt[i].gran = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access = access;
}

void gdt_init(void) {
    set_gate(0, 0, 0, 0, 0);
    set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* code */
    set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* data */
    gp.limit = sizeof(gdt) - 1; gp.base = (uint32_t)&gdt;
    gdt_flush((uint32_t)&gp);
}
