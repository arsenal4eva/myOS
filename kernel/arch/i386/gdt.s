; GDT - flat 32-bit segments, all in assembly.
; Layout: [0] null, [1] kernel code (0x08), [2] kernel data (0x10)
; Descriptors: base=0, limit=0xFFFFF, gran=4K/32-bit (0xCF), code=0x9A, data=0x92

global gdt_init
global gdt_flush

section .data
align 8
gdt_start:
    dq 0x0000000000000000          ; null
    dq 0x00CF9A000000FFFF          ; code: base 0, limit 4GB, exec/read, ring0
    dq 0x00CF92000000FFFF          ; data: base 0, limit 4GB, read/write, ring0
gdt_end:

gp:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .text
; void gdt_flush(uint32_t ptr) -- reload from arbitrary pointer (compat)
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

; void gdt_init(void) -- load built-in table above
gdt_init:
    lgdt [gp]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret
