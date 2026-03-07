; MiniDOS Simple Second-Stage Bootloader
; Loaded by MBR at 0x7E00
; Just loads kernel and jumps to it

[BITS 16]
[ORG 0x7E00]

%define BOOT_VIDEO_FLAG        0x0510
%define BOOT_VIDEO_WIDTH       0x0512
%define BOOT_VIDEO_HEIGHT      0x0514
%define BOOT_VIDEO_PITCH       0x0516
%define BOOT_VIDEO_BPP         0x0518
%define BOOT_VIDEO_RED_SIZE    0x0519
%define BOOT_VIDEO_RED_POS     0x051A
%define BOOT_VIDEO_GREEN_SIZE  0x051B
%define BOOT_VIDEO_GREEN_POS   0x051C
%define BOOT_VIDEO_BLUE_SIZE   0x051D
%define BOOT_VIDEO_BLUE_POS    0x051E
%define BOOT_VIDEO_FB          0x0520
%define BOOT_DRIVE_NUMBER      0x0504
%define BOOT_DRIVE_FLAGS       0x0505
%define BOOT_DRIVE_SPT         0x0506
%define BOOT_DRIVE_HEADS       0x0508

%define VBE_MODE_INFO          0x7000

start:
    ; Save drive number from DL
    mov [drive_number], dl
    
    ; Initialize serial
    call serial_init

    ; Detect INT 13h extensions (LBA). Skip for floppy drives.
    mov byte [lba_supported], 0
    mov dl, [drive_number]
    cmp dl, 0x80
    jb .lba_check_done
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .lba_check_done
    cmp bx, 0xAA55
    jne .lba_check_done
    test cx, 0x0001
    jz .lba_check_done
    mov byte [lba_supported], 1
.lba_check_done:

    ; Query BIOS drive geometry for CHS translation
    mov ah, 0x08
    mov dl, [drive_number]
    int 0x13
    jc .geom_done
    and cl, 0x3F            ; sectors per track (lower 6 bits)
    xor ch, ch
    mov [sectors_per_track], cx
    movzx cx, dh            ; heads = max head + 1
    inc cx
    mov [heads], cx
    mov byte [geom_ok], 1
.geom_done:
    ; Fallback geometry for floppy if BIOS query failed
    cmp byte [geom_ok], 0
    jne .geom_done2
    cmp byte [drive_number], 0x80
    jae .geom_done2
    mov word [sectors_per_track], 18
    mov word [heads], 2
    mov byte [geom_ok], 1
.geom_done2:
    
    ; Read base memory from BIOS (0x413 contains KB of base memory, max 640KB)
    xor eax, eax
    mov ax, [0x413]         ; Read base memory KB from BIOS data area
    mov [0x500], ax         ; Store at 0x500 for kernel
    
    ; Read extended memory using INT 0x15, AH=0x88 (returns KB above 1MB)
    xor eax, eax
    mov ah, 0x88
    int 0x15
    jc .no_extended         ; If carry set, no extended memory
    mov [0x502], ax         ; Store extended memory KB at 0x502
    jmp .mem_done
.no_extended:
    mov word [0x502], 0     ; No extended memory
.mem_done:

    ; Publish boot drive metadata for the protected-mode kernel.
    mov al, [drive_number]
    mov [BOOT_DRIVE_NUMBER], al

    xor al, al
    cmp byte [lba_supported], 0
    je .no_lba_flag
    or al, 0x01
.no_lba_flag:
    cmp byte [geom_ok], 0
    je .no_geom_flag
    or al, 0x02
.no_geom_flag:
    mov [BOOT_DRIVE_FLAGS], al

    mov ax, [sectors_per_track]
    mov [BOOT_DRIVE_SPT], ax
    mov ax, [heads]
    mov [BOOT_DRIVE_HEADS], ax
    
    mov si, msg_start
    call serial_print_string
    
    ; Show boot logo
    mov si, msg_before_logo
    call serial_print_string
    
    call show_boot_logo
    
    mov si, msg_after_logo
    call serial_print_string
    
    ; Load kernel from sector 5 (after MBR + stage2)
    mov si, msg_loading_kernel
    call serial_print_string
    
    mov ax, 5               ; Start at sector 5
    mov bx, 0x1000           ; Load at 0x1000:0000 = physical 0x10000
    mov es, bx
    xor bx, bx

    mov cx, [kernel_sectors] ; Load kernel sectors (patched at build time)
    cmp cx, 0
    jne .load_loop
    mov cx, 32               ; Fallback if not patched

.load_loop:
    push cx
    push ax
    push bx
    
    mov cx, 1
    call disk_read
    
    mov al, '.'
    mov ah, 0x0E
    int 0x10

    pop bx
    pop ax
    pop cx
    
    inc ax
    add bx, 512
    loop .load_loop

    mov si, msg_loaded
    call serial_print_string

    ; Enable A20 Line
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Disable interrupts and load GDT
    mov si, msg_pm
    call serial_print_string
    
    cli

    mov si, msg_pm_cli
    call serial_print_string
    
    ; Load GDT (absolute address via ORG 0x7E00)
    lgdt [gdt_desc]

    mov si, msg_pm_lgdt
    call serial_print_string

    ; Set Protected Mode bit
    mov si, msg_pm_cr0
    call serial_print_string
    mov si, msg_before_jump
    call serial_print_string
    call serial_wait_tx_empty
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to flush prefetch and enter 32-bit mode
    jmp 0x08:pm_start

; Configure video mode for kernel (prefer VESA LFB, fallback to text mode)
show_boot_logo:
    pusha
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov es, ax
    cld

    ; Default values for text mode fallback
    mov byte [BOOT_VIDEO_FLAG], 0
    mov word [BOOT_VIDEO_WIDTH], 80
    mov word [BOOT_VIDEO_HEIGHT], 25
    mov word [BOOT_VIDEO_PITCH], 160
    mov byte [BOOT_VIDEO_BPP], 0
    mov dword [BOOT_VIDEO_FB], 0

    mov si, vesa_modes
.try_next_mode:
    lodsw
    cmp ax, 0xFFFF
    je .fallback_text
    mov [vesa_mode_selected], ax

    mov cx, ax
    mov ax, 0x4F01
    mov di, VBE_MODE_INFO
    int 0x10
    cmp ax, 0x004F
    jne .try_next_mode

    mov bx, [VBE_MODE_INFO + 0x00] ; ModeAttributes
    test bx, 0x0001                ; Mode supported
    jz .try_next_mode
    test bx, 0x0080                ; Linear framebuffer supported
    jz .try_next_mode

    mov ax, 0x4F02
    mov bx, [vesa_mode_selected]
    or bx, 0x4000                  ; request LFB
    int 0x10
    cmp ax, 0x004F
    jne .try_next_mode

    ; Publish VESA mode metadata for the kernel
    mov byte [BOOT_VIDEO_FLAG], 1
    mov ax, [VBE_MODE_INFO + 0x12] ; XResolution
    mov [BOOT_VIDEO_WIDTH], ax
    mov ax, [VBE_MODE_INFO + 0x14] ; YResolution
    mov [BOOT_VIDEO_HEIGHT], ax
    mov ax, [VBE_MODE_INFO + 0x10] ; BytesPerScanLine
    mov [BOOT_VIDEO_PITCH], ax
    mov al, [VBE_MODE_INFO + 0x19] ; BitsPerPixel
    mov [BOOT_VIDEO_BPP], al
    mov al, [VBE_MODE_INFO + 0x1F] ; RedMaskSize
    mov [BOOT_VIDEO_RED_SIZE], al
    mov al, [VBE_MODE_INFO + 0x20] ; RedFieldPosition
    mov [BOOT_VIDEO_RED_POS], al
    mov al, [VBE_MODE_INFO + 0x21] ; GreenMaskSize
    mov [BOOT_VIDEO_GREEN_SIZE], al
    mov al, [VBE_MODE_INFO + 0x22] ; GreenFieldPosition
    mov [BOOT_VIDEO_GREEN_POS], al
    mov al, [VBE_MODE_INFO + 0x23] ; BlueMaskSize
    mov [BOOT_VIDEO_BLUE_SIZE], al
    mov al, [VBE_MODE_INFO + 0x24] ; BlueFieldPosition
    mov [BOOT_VIDEO_BLUE_POS], al
    mov eax, [VBE_MODE_INFO + 0x28] ; PhysBasePtr
    mov [BOOT_VIDEO_FB], eax
    jmp .done

.fallback_text:
    mov ax, 0x0003
    int 0x10

.done:
    pop es
    pop ds
    popa
    ret

; Serial port functions (COM1)
serial_init:
    mov dx, 0x3F8 + 3       ; Line control
    mov al, 0x80            ; DLAB
    out dx, al
    
    mov dx, 0x3F8
    mov al, 0x03            ; Divisor = 3 (38400 baud)
    out dx, al
    
    mov dx, 0x3F8 + 1
    xor al, al
    out dx, al
    
    mov dx, 0x3F8 + 3       ; Line control
    mov al, 0x03            ; 8N1
    out dx, al
    ret

serial_print_string:
    ; SI = string
.loop:
    lodsb
    or al, al
    jz .done
    
    mov dx, 0x3F8 + 5       ; Line status
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    
    mov al, [si - 1]
    mov dx, 0x3F8
    out dx, al
    jmp .loop
.done:
    ret

serial_wait_tx_empty:
    mov dx, 0x3F8 + 5
.wait:
    in al, dx
    test al, 0x40
    jz .wait
    ret

; Read disk sector (LBA if supported, CHS fallback)
; AX = LBA sector, CX = 1
disk_read:
    pusha

    cmp byte [lba_supported], 0
    je .chs_fallback

    ; Try INT 13h extensions (LBA)
    push ds
    xor dx, dx
    mov ds, dx
    mov word [dap + 2], 1       ; sectors
    mov word [dap + 4], bx      ; offset
    mov word [dap + 6], es      ; segment
    mov word [dap + 8], ax      ; LBA low
    mov word [dap + 10], dx     ; LBA high (0)
    mov dword [dap + 12], 0     ; LBA upper 32
    mov si, dap
    mov dl, [drive_number]
    mov ah, 0x42
    int 0x13
    pop ds
    jnc .done

    ; LBA failed (likely floppy or unsupported) -> disable and fallback to CHS
    mov byte [lba_supported], 0

    ; CHS fallback
.chs_fallback:
    xor dx, dx
    div word [sectors_per_track]
    inc dx
    mov cl, dl              ; Sector

    xor dx, dx
    div word [heads]
    mov dh, dl              ; Head
    mov ch, al              ; Cylinder

    mov al, 1               ; Sector count
    mov dl, [drive_number]
    mov ah, 0x02            ; Read
    int 0x13

.done:
    popa
    ret

[BITS 32]
pm_start:
    ; Set up all segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Jump to kernel entry (linked at 0x10000)
    jmp 0x10000

; --- Data ---
gdt_start:
    dq 0x0                  ; NULL descriptor
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 0b10011010, 0b11001111, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 0b10010010, 0b11001111, 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

drive_number: db 0x80
lba_supported: db 0
geom_ok: db 0
sectors_per_track: dw 63
heads: dw 255
kernel_sectors: dw 32

dap:
    db 0x10                 ; size of DAP
    db 0x00                 ; reserved
    dw 0x0001               ; sectors to read
    dw 0x0000               ; offset
    dw 0x0000               ; segment
    dd 0x00000000           ; LBA low
    dd 0x00000000           ; LBA high

msg_start: db '[Stage2] Started', 0x0D, 0x0A, 0
msg_before_logo: db '[Stage2] Displaying boot logo...', 0x0D, 0x0A, 0
msg_after_logo: db '[Stage2] Logo displayed', 0x0D, 0x0A, 0
msg_loading_kernel: db '[Stage2] Loading kernel...', 0x0D, 0x0A, 0
msg_loaded: db '[Stage2] Kernel loaded', 0x0D, 0x0A, 0
msg_pm: db '[Stage2] Entering PM...', 0x0D, 0x0A, 0
msg_pm_cli: db '[Stage2] PM checkpoint 1: CLI', 0x0D, 0x0A, 0
msg_pm_lgdt: db '[Stage2] PM checkpoint 2: LGDT', 0x0D, 0x0A, 0
msg_pm_cr0: db '[Stage2] PM checkpoint 3: set CR0.PE', 0x0D, 0x0A, 0
msg_before_jump: db '[Stage2] Before far jump', 0x0D, 0x0A, 0

vesa_mode_selected: dw 0
vesa_modes:
    dw 0x112                ; 640x480
    dw 0x111                ; 640x480
    dw 0x115                ; 800x600 fallback
    dw 0x114                ; 800x600 fallback
    dw 0x118                ; 1024x768 fallback
    dw 0x117                ; 1024x768 fallback
    dw 0xFFFF

times 2048-($-$$) db 0      ; Pad to 2048 bytes
