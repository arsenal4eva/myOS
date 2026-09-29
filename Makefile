CC = gcc
AS = nasm
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector \
         -fno-pic -fno-pie -Iinclude
LDFLAGS = -m32 -ffreestanding -nostdlib -no-pie -Wl,-T,kernel/arch/i386/linker.ld -Wl,-m,elf_i386

OBJS = kernel/arch/i386/boot.o kernel/arch/i386/isr.o \
       kernel/kernel.o kernel/string.o kernel/tty.o kernel/serial.o \
       kernel/kprintf.o kernel/gdt.o kernel/idt.o kernel/timer.o \
         kernel/keyboard.o kernel/pmm.o kernel/paging.o kernel/kmalloc.o \
         kernel/fs.o kernel/shell.o kernel/sysinfo.o

all: idkos.bin

%.o: %.s
	$(AS) -f elf32 $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

idkos.bin: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

grub-check: idkos.bin
	grub-file --is-x86-multiboot idkos.bin && echo MULTIBOOT_OK

iso: idkos.bin
	mkdir -p iso/boot
	cp idkos.bin iso/boot/myos.bin
	grub-mkrescue -o snackos.iso iso/

qemu: idkos.bin
	qemu-system-i386 -kernel idkos.bin -serial stdio -m 128M

qemu-iso: iso
	qemu-system-i386 -cdrom snackos.iso -serial stdio -m 128M

qemu-debug: idkos.bin
	qemu-system-i386 -s -S -kernel idkos.bin -serial stdio -m 128M

clean:
	rm -f $(OBJS) idkos.bin snackos.iso

.PHONY: all grub-check iso qemu qemu-iso qemu-debug clean
