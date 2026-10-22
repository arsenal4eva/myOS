; Stage 1: 512-byte boot sector. Loads stage2 (8 sectors from LBA 1) to 0x7E00.
BITS 16
ORG 0x7C00

STAGE2_LBA    equ 1
STAGE2_SECTS  equ 16
STAGE2_ADDR   equ 0x7E00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [disk_drive], dl

    mov si, msg_boot
    call print

    ; Use EDD packet to load stage2
    mov ah, 0x42
    mov dl, [disk_drive]
    mov si, dap
    int 0x13
    jc disk_error

    mov dl, [disk_drive]
    jmp 0x0000:STAGE2_ADDR

disk_error:
    mov si, msg_err
    call print
.hang:
    cli
    hlt
    jmp .hang

print: ; SI = string
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .loop
.done:
    popa
    ret

msg_boot db 'myOS boot1...', 13, 10, 0
msg_err  db 'DISK ERR', 13, 10, 0

align 4
dap:
    db 0x10, 0x00
    dw STAGE2_SECTS
    dw STAGE2_ADDR, 0x0000
    dd STAGE2_LBA
    dd 0

disk_drive db 0

times 510-($-$$) db 0
dw 0xAA55
