; MiniDOS Simple Second-Stage Bootloader
; Loaded by MBR at 0x7E00
; Just loads kernel and jumps to it

[BITS 16]
[ORG 0x7E00]

%define PAL_LBA       98
%define PAL_SECTORS   2
%define PAL_BUFFER    0x0600
%define LOGO_LBA      100
%define LOGO_SECTORS  125
%define LOGO_BUF_SEG  0x9000

start:
    ; Save drive number from DL
    mov [drive_number], dl
    
    ; Initialize serial
    call serial_init

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
.geom_done:
    
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
    
    mov si, msg_start
    call serial_print_string
    
    ; Show boot logo
    mov si, msg_before_logo
    call serial_print_string
    
    call show_boot_logo
    
    mov si, msg_after_logo
    call serial_print_string
    
    ; Load kernel from sector 3 (after MBR + stage2)
    mov si, msg_loading_kernel
    call serial_print_string
    
    mov cx, 20              ; Load 20 sectors
    mov ax, 3               ; Start at sector 3
    mov bx, 0x0100           ; Load at 0x0100:0000 = physical 0x1000
    mov es, bx
    xor bx, bx

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
    
    ; Load GDT (absolute address via ORG 0x7E00)
    lgdt [gdt_desc]

    ; Set Protected Mode bit
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to flush prefetch and enter 32-bit mode
    jmp 0x08:pm_start

; Show boot logo in VGA Mode 13h
show_boot_logo:
    pusha
    push ds
    push es
    cld
    
    xor ax, ax
    mov ds, ax
    
    ; Switch to VGA Mode 13h (320x200, 256 colors)
    mov ax, 0x0013
    int 0x10

    ; Load palette into low memory buffer
    xor ax, ax
    mov es, ax
    mov bx, PAL_BUFFER
    mov ax, PAL_LBA
    mov cx, PAL_SECTORS
.pal_loop:
    call disk_read
    add bx, 512
    inc ax
    loop .pal_loop

    ; Program VGA DAC palette (256 * 3 bytes)
    mov dx, 0x3C8
    xor al, al
    out dx, al
    mov dx, 0x3C9
    mov si, PAL_BUFFER
    mov cx, 256*3
.pal_out:
    lodsb
    out dx, al
    loop .pal_out
    
    ; Load raw logo from disk into a safe buffer first
    mov ax, LOGO_BUF_SEG
    mov es, ax
    xor bx, bx
    mov ax, LOGO_LBA
    mov cx, LOGO_SECTORS
.logo_loop:
    call disk_read
    add bx, 512
    inc ax
    loop .logo_loop

    ; Copy logo buffer to VGA memory
    mov ax, LOGO_BUF_SEG
    mov ds, ax
    mov ax, 0xA000
    mov es, ax
    xor si, si
    xor di, di
    mov cx, 64000
    rep movsb
    
    xor ax, ax
    mov ds, ax
    
    ; SHORT delay - 3 seconds
    mov bp, 9
.delay_outer3:
    mov cx, 0xFFFF
.delay_outer2:
    push cx
    mov cx, 0x0FFF
.delay_inner:
    nop
    nop
    nop
    nop
    loop .delay_inner
    pop cx
    loop .delay_outer2
    dec bp
    jnz .delay_outer3
    
    ; Return to text mode
    mov ax, 0x0003
    int 0x10
    
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

; Read disk sector (LBA if supported, CHS fallback)
; AX = LBA sector, CX = 1
disk_read:
    pusha

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

    ; CHS fallback
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

    ; Jump to kernel entry (linked at 0x1000)
    jmp 0x1000

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
sectors_per_track: dw 63
heads: dw 255

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
msg_before_jump: db '[Stage2] Before far jump', 0x0D, 0x0A, 0

times 1024-($-$$) db 0      ; Pad to 1024 bytes
