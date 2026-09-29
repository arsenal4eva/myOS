#include "kernel/multiboot.h"
#include "kernel/tty.h"
#include "kernel/serial.h"
#include "kernel/kprintf.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/timer.h"
#include "kernel/keyboard.h"
#include "kernel/pmm.h"
#include "kernel/paging.h"
#include "kernel/kmalloc.h"
#include "kernel/shell.h"
#include "kernel/port.h"
#include "kernel/fs.h"

extern uint32_t _kernel_end;

void kernel_main(uint32_t magic, multiboot_info_t *mbi) {
    tty_init(); serial_init();
    tty_setcolor(0x0B, 0x00);
    tty_write("=== Welcome to myOS (idk i couldnt think of a better name) ===\n");
    tty_setcolor(0x07, 0x00);

    if (magic != MULTIBOOT_MAGIC) {
        kprintf("[boot] BAD multiboot magic: 0x%x (expected 0x%x)\n", magic, MULTIBOOT_MAGIC);
        for (;;) __asm__ volatile("cli; hlt");
    }
    kprintf("[boot] magic ok, mbi=%p flags=0x%x\n", (void *)mbi, mbi->flags);
    kprintf("[boot] mem: lower=%dK upper=%dK\n", mbi->mem_lower, mbi->mem_upper);

    gdt_init();  kprintf("[gdt] flat segments loaded\n");
    idt_init();  kprintf("[idt] 32 ISRs + 16 IRQs, PIC remapped\n");
    timer_init(100); kprintf("[pit] 100 Hz\n");
    keyboard_init(); kprintf("[kbd] ps/2 ready\n");

    pmm_init(mbi, (uint32_t)&_kernel_end);
    paging_init();
    heap_init();
    fs_init(); kprintf("[fs] ramfs ready (64 entries, 4K/file)\n");

    void *t = kmalloc(32);
    kprintf("[selftest] kmalloc smoke: %p -> %s\n", t, t ? "OK" : "FAIL");
    kfree(t);

    __asm__ volatile("sti");
    kprintf("[boot] interrupts on. entering shell.\n\n");
    shell_run();
    for (;;) __asm__ volatile("hlt");
}
