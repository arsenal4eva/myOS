; IDT - 256 gate table, PIC remap, and IRQ dispatch table, all in assembly.
; Gates 0-31  -> isr0..isr31 (sel 0x08, flags 0x8E, 32-bit interrupt gate, ring0)
; Gates 32-47 -> irq0..irq15 (same sel/flags), PIC remapped to 0x20/0x28
; C handlers: isr_handler(regs_t*), irq_handler(regs_t*) live in kernel/idt.c.
; irq_routines[16] lives here; irq_install/uninstall implemented here.

global idt_init
global idt_flush
global idt_set_gate
global irq_install_handler
global irq_uninstall_handler
global irq_routines

extern isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
extern isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
extern isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
extern isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
extern irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
extern irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

section .data
align 8
idt:
    times 256 dq 0
idt_end:

ip:
    dw idt_end - idt - 1
    dd idt

; Address tables for the init loop (link-time addresses)
isr_table:
    dd isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dd isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
irq_table:
    dd irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    dd irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

align 4
irq_routines:
    times 16 dd 0

section .text

; void io_wait_asm(void) -- delay via port 0x80
io_wait_asm:
    push eax
    mov al, 0
    out 0x80, al
    pop eax
    ret

; void outb_asm(uint16_t port, uint8_t val)
;   [esp+4]=port, [esp+8]=val
outb_asm:
    push eax
    push edx
    mov dx, [esp + 12]
    mov al, [esp + 16]
    out dx, al
    pop edx
    pop eax
    ret

; void idt_set_gate(uint8_t n, uint32_t base, uint16_t sel, uint8_t flags)
;   [esp+4]=n, [esp+8]=base, [esp+12]=sel, [esp+16]=flags
idt_set_gate:
    push ebx
    push eax
    push edx
    movzx ebx, byte [esp + 16]       ; n
    shl ebx, 3                       ; n * 8
    add ebx, idt
    mov eax, [esp + 20]              ; base
    mov edx, eax
    mov word [ebx], ax               ; base_lo
    shr edx, 16
    mov word [ebx + 6], dx           ; base_hi
    mov ax, [esp + 24]               ; sel
    mov word [ebx + 2], ax
    mov byte [ebx + 4], 0            ; always0
    mov al, [esp + 28]               ; flags
    mov byte [ebx + 5], al
    pop edx
    pop eax
    pop ebx
    ret

; void idt_flush(uint32_t ptr)
idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; static void pic_remap(void) -- 8259A ICW1-4, vectors 0x20/0x28, masks cleared
pic_remap:
    push 0x11
    push 0x20
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x11
    push 0xA0
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x20
    push 0x21
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x28
    push 0xA1
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x04
    push 0x21
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x02
    push 0xA1
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x01
    push 0x21
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x01
    push 0xA1
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x00
    push 0x21
    call outb_asm
    add esp, 8
    call io_wait_asm
    push 0x00
    push 0xA1
    call outb_asm
    add esp, 8
    call io_wait_asm
    ret

; void idt_init(void)
idt_init:
    push ebx
    push esi
    push edi
    ; zero the table (rep stosd over 256*8 bytes)
    mov edi, idt
    xor eax, eax
    mov ecx, 256*2
    rep stosd
    ; gates 0..31 from isr_table
    xor ebx, ebx                  ; i = 0
.isr_loop:
    cmp ebx, 32
    jge .irq_setup
    mov eax, [isr_table + ebx*4]  ; base
    mov edx, eax
    mov esi, ebx
    shl esi, 3
    add esi, idt
    mov word [esi], ax
    shr edx, 16
    mov word [esi + 6], dx
    mov word [esi + 2], 0x08
    mov byte [esi + 4], 0
    mov byte [esi + 5], 0x8E
    inc ebx
    jmp .isr_loop
.irq_setup:
    call pic_remap
    ; gates 32..47 from irq_table
    xor ebx, ebx
.irq_loop:
    cmp ebx, 16
    jge .load
    mov eax, [irq_table + ebx*4]
    mov edx, eax
    mov esi, ebx
    add esi, 32
    shl esi, 3
    add esi, idt
    mov word [esi], ax
    shr edx, 16
    mov word [esi + 6], dx
    mov word [esi + 2], 0x08
    mov byte [esi + 4], 0
    mov byte [esi + 5], 0x8E
    inc ebx
    jmp .irq_loop
.load:
    lidt [ip]
    pop edi
    pop esi
    pop ebx
    ret

; void irq_install_handler(int irq, void (*h)(regs_t*))
irq_install_handler:
    mov eax, [esp + 4]     ; irq
    cmp eax, 0
    jl .done
    cmp eax, 16
    jge .done
    mov edx, [esp + 8]     ; handler
    mov [irq_routines + eax*4], edx
.done:
    ret

; void irq_uninstall_handler(int irq)
irq_uninstall_handler:
    mov eax, [esp + 4]
    cmp eax, 0
    jl .done
    cmp eax, 16
    jge .done
    mov dword [irq_routines + eax*4], 0
.done:
    ret
