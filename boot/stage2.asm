; Stage 2: loaded at 0x7E00 by stage1.
; Real-mode: BEEP via PC speaker, E820 map, mem size, A20, load kernel ELF,
; enter protected mode, ELF-load kernel to 1M, build multiboot info, jump.
; Kernel location on disk patched by Makefile: KERNEL_LBA / KERNEL_SECTS.
BITS 16
ORG 0x7E00

%ifndef KERNEL_LBA
%define KERNEL_LBA 32
%endif
%ifndef KERNEL_SECTS
%define KERNEL_SECTS 256
%endif

MMAP_ADDR    equ 0xA000      ; E820 entries (multiboot fmt); must be clear of stage2 (ends 0x9E00)
KLOAD_TMP    equ 0x10000     ; bounce buffer for kernel ELF image
MBINFO_ADDR  equ 0x500       ; multiboot_info struct
STACK32      equ 0x90000

start16:
    mov [disk_drive], dl
    mov si, msg_stage2
    call print16

    call beep               ; <-- startup beep on PC speaker
    mov si, msg_beep
    call print16

    call enable_a20
    call get_mem_e820
    call get_mem_size
    call load_kernel

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

; ---------------- 16-bit helpers ----------------
print16:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp .loop
.done:
    popa
    ret

; PC speaker beep: ~880 Hz for ~300 ms via PIT channel 2
beep:
    pusha
    mov al, 0xB6
    out 0x43, al
    mov ax, 1356            ; 1193180/880
    out 0x42, al
    mov al, ah
    out 0x42, al
    in al, 0x61
    or al, 3
    out 0x61, al
    ; delay ~300ms: BIOS wait (CX:DX microseconds)
    mov ah, 0x86
    mov cx, 0x0004
    mov dx, 0x93E0          ; 0x493E0 = 300000 us
    int 0x15
    in al, 0x61
    and al, 0xFC
    out 0x61, al
    popa
    ret

enable_a20:
    pusha
    in al, 0x92             ; fast A20
    or al, 2
    out 0x92, al
    popa
    ret

; E820 memory map -> MMAP_ADDR in multiboot format (size=20), count in [mmap_count]
get_mem_e820:
    pusha
    xor ebx, ebx
    mov di, MMAP_ADDR
    mov word [mmap_count], 0
.next:
    mov [sp_save], sp
    xor ax, ax
    mov es, ax              ; BIOS writes to ES:DI
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    mov sp, [sp_save]       ; tolerate BIOSes that drift SP across continuation calls
    jc .done
    cmp eax, 0x534D4150
    jne .done
    ; Convert BIOS entry (base 8B, len 8B, type 4B[, attrs]) to multiboot
    ; (size 4B, base 8B, len 8B, type 4B): shift first 20 bytes up by 4.
    push di
    movzx esi, di
    add esi, 19
    mov edi, esi
    add edi, 4
    mov cx, 20
    std
    rep movsb
    cld
    pop di
    mov dword [di], 20      ; size field
    inc word [mmap_count]
    add di, 24
    test ebx, ebx
    jnz .next
.done:
    popa
    ret

; mem_lower (KB 0-640) via int12; mem_upper via E801 (fallback AH=88)
get_mem_size:
    pusha
    int 0x12                ; AX = contiguous KB
    mov [mem_lower], ax
    mov eax, 0xE801
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    int 0x15
    jc .fallback
    ; AX = KB 1M-16M, BX = 64K pages above 16M
    movzx eax, ax
    mov [mem_upper], ax
    movzx ebx, bx
    shl ebx, 6              ; 64K pages -> KB
    mov cx, bx
    add [mem_upper], cx
    jmp .done
.fallback:
    mov ah, 0x88
    int 0x15                ; AX = KB above 1M
    mov [mem_upper], ax
.done:
    popa
    ret

; Load kernel ELF from disk to KLOAD_TMP via EDD reads in <=32-sector chunks
load_kernel:
    pusha
    mov si, msg_load
    call print16
    mov eax, KERNEL_LBA
    mov [cur_lba], eax
    mov dword [seg_lin], KLOAD_TMP
    mov word [seg_seg], 0x1000
    mov word [seg_addr], 0x0000
    mov cx, KERNEL_SECTS    ; remaining
    mov [remaining], cx
.chunk:
    test cx, cx
    jz .done
    cmp cx, 32
    jbe .have_n
    mov word [dap_count], 32
    jmp .do_read
.have_n:
    mov [dap_count], cx
.do_read:
    mov eax, [cur_lba]
    mov [dap_lba_lo], eax
    mov ax, [seg_addr]
    mov [dap_off], ax
    mov ax, [seg_seg]
    mov [dap_seg], ax
    mov ah, 0x42
    mov dl, [disk_drive]
    mov si, dap
    int 0x13
    jc .err
    movzx eax, word [dap_count]
    add [cur_lba], eax
    ; advance buffer pointer by count*512
    movzx eax, word [dap_count]
    shl eax, 9
    add [seg_lin], eax
    mov eax, [seg_lin]
    sub eax, KLOAD_TMP
    mov ebx, eax
    shr eax, 4              ; paragraphs from base
    add eax, 0x1000
    mov [seg_seg], ax
    mov eax, ebx
    and eax, 0xF
    mov [seg_addr], ax
    mov cx, [remaining]
    sub cx, [dap_count]
    mov [remaining], cx
    mov cx, [remaining]
    jmp .chunk
.err:
    mov si, msg_derr
    call print16
.hang:
    cli
    hlt
    jmp .hang
.done:
    popa
    ret

msg_stage2 db 'myOS boot2...', 13, 10, 0
msg_beep   db 'beep!', 13, 10, 0
msg_load   db 'loading kernel...', 13, 10, 0
msg_derr   db 'KERN DISK ERR', 13, 10, 0

disk_drive dw 0
mmap_count dw 0
mem_lower  dw 640
mem_upper  dw 0
sp_save    dw 0
cur_lba    dd 0
seg_lin    dd KLOAD_TMP
seg_addr   dw 0
seg_seg    dw 0x1000
remaining  dw 0

align 4
dap:
    db 0x10, 0
dap_count: dw 0
dap_off: dw 0
dap_seg: dw 0
dap_lba_lo: dd 0
dap_lba_hi: dd 0

align 4
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF   ; code 0-4G
    dq 0x00CF92000000FFFF   ; data 0-4G
gdt_desc:
    dw gdt_desc - gdt_start - 1
    dd gdt_start

; ---------------- 32-bit ----------------
BITS 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STACK32

    ; Verify ELF magic at KLOAD_TMP
    mov esi, KLOAD_TMP
    cmp dword [esi], 0x464C457F
    jne elf_fail

    ; Copy LOAD segments: e_phoff [28], e_phnum [44](w), e_phentsize [42](w)
    mov ebx, [esi+28]       ; phoff
    add ebx, esi            ; ph table
    movzx ecx, word [esi+44]; phnum
    movzx edx, word [esi+42]; phentsize
    test ecx, ecx
    jz elf_fail
.ph_loop:
    mov eax, [ebx]          ; p_type
    cmp eax, 1              ; PT_LOAD?
    jne .next
    push ecx
    push edx
    push ebx
    mov edi, [ebx+12]       ; p_paddr (dest)
    mov ecx, [ebx+16]       ; p_filesz
    mov eax, [ebx+4]        ; p_offset
    add eax, esi            ; src
    push esi
    mov esi, eax
    test ecx, ecx
    jz .nofilesz
    rep movsb
.nofilesz:
    pop esi
    pop ebx
    push ebx
    mov ecx, [ebx+20]       ; p_memsz
    sub ecx, [ebx+16]       ; - filesz
    xor al, al
    test ecx, ecx
    jz .nomemsz
    rep stosb
.nomemsz:
    pop ebx
    pop edx
    pop ecx
.next:
    add ebx, edx
    loop .ph_loop

    ; Build multiboot info at MBINFO_ADDR (mind the 16-byte syms/tab union!)
    movzx eax, word [mmap_count]
    imul eax, eax, 24
    mov dword [MBINFO_ADDR+44], eax   ; mmap_length (offset 44)
    mov dword [MBINFO_ADDR+48], MMAP_ADDR ; mmap_addr (offset 48)
    mov dword [MBINFO_ADDR], 0x41    ; flags: bit0 mem + bit6 mmap
    movzx eax, word [mem_lower]
    mov [MBINFO_ADDR+4], eax
    movzx eax, word [mem_upper]
    mov [MBINFO_ADDR+8], eax

    ; Entry point [24], args: eax=magic, ebx=mbi
    mov ecx, [esi+24]
    mov eax, 0x2BADB002
    mov ebx, MBINFO_ADDR
    jmp ecx

elf_fail:
    ; red screen + halt
    mov edi, 0xB8000
    mov eax, 0x4F204F20     ; white on red spaces
    mov ecx, 1000
    rep stosd
.hang:
    cli
    hlt
    jmp .hang

times 8192-($-$$) db 0   ; stage2 fixed 8K (16 sectors)
