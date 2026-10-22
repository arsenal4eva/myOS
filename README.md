# idkOS 

A 32-bit x86 hobby OS: Multiboot1 + GRUB, monolithic kernel, VGA text console + serial log, GDT/IDT/PIC, PIT @100Hz, PS/2 keyboard, and an interactive `idk>` shell.

## Layout

```
~/Projects/myOS/
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

Debug: `make qemu-debug` + `gdb -ex 'target remote :1234' idkos.bin`.

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


