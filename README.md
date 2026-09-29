# idkOS 

A 32-bit x86 hobby OS: Multiboot1 + GRUB, monolithic kernel, VGA text console + serial log, GDT/IDT/PIC, PIT @100Hz, PS/2 keyboard, and an interactive `idk>` shell.

## Layout

```
~/Project/myOS/
  Makefile
  kernel/arch/i386/{boot.s,isr.s,linker.ld}
  kernel/{kernel,shell,tty,serial,kprintf,string,gdt,idt,timer,keyboard,pmm,paging,kmalloc,fs,sysinfo}.c
  include/kernel/*.h
  iso/boot/grub/grub.cfg
```

## Build & run

```bash
make            
make grub-check 
make qemu       
```

> `make iso` needs `xorriso` (not installed here); `myos.bin` also boots
> via GRUB (`multiboot /boot/myos.bin`) on any machine with grub-mkrescue.

Debug: `make qemu-debug` + `gdb -ex 'target remote :1234' myos.bin`.

## Shell commands

help | sysinfo | man | echo <text> | clear | ticks | beep |
create <file...> | mkdir <dir...> | list [path...] | print <file...> | remove <path...>

Files live in a RAM filesystem (64 entries, 4 KiB/file, `a/b.txt` paths OK).
Redirection: `echo hello > hi.txt`, `echo more >> hi.txt`, `print < hi.txt`
(any command's output redirects, e.g. `sysinfo > info.txt`).
Globbing: `*` any run, `?` one char — `list *.txt`, `print docs/*.txt`;
quote to suppress (`echo "*"`).
Chaining: `;` runs commands in order — `echo hi ; ticks`;
`;` inside quotes or escaped as `\;` stays literal.
Try: `create hi.txt`, `echo hello > hi.txt`,
`print hi.txt`, `list *.txt`.
Chained: `create hi.txt ; echo hello > hi.txt ; print hi.txt ; list *.txt`.

## Design notes

- Boot: GRUB Multiboot1, 16K stack in .bss, magic 0x2BADB002 checked.
- Interrupts: PIC remapped 0x20/0x28, ISR stubs in NASM push err/int_no,
  C handlers; EOI to 0x20/0xA0.
- Memory: PMM bitmap (up to 4GiB frames) from mmap, kernel reserved;
  paging maps low 4M identity + higher-half; heap grows via sbrk pages.
- Freestanding: own string.c, kprintf (%d %u %x %X %p %s %c), no libc.
- Console: tty.c (80x25 VGA text, colors, cursor, scrolling), serial.c
  (COM1 38400 8N1 log + input), keyboard.c (PS/2 scancodes, ASCII +
  KEY UP/DOWN/LEFT/RIGHT/HOME/END/PGUP/PGDN/DELETE, Ctrl+A-Z as 1-26).
- Timing/sound: timer.c (PIT @100Hz, ticks/sleep), `ticks` prints uptime,
  `beep` drives the PC speaker.
- Filesystem: fs.c (RAM fs, 64 entries, 4 KiB/file, dirs, glob `*`/`?` match).
- Shell: shell.c (`idk>` with quoting, `<`/`>`/`>>` redirection, `;` chaining,
  glob expansion, capture-to-file) + commands listed above.
- Sysinfo: sysinfo.c (fastfetch-style: CPUID vendor/brand/cores/MHz, PCI
  scan for GPU/net/audio/host, SMBIOS host/BIOS, ACPI battery check,
  ATA identify, PMM usage, uptime, palette).
