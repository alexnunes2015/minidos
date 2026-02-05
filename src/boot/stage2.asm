; MiniDOS Second-Stage Bootloader
; Loaded by MBR at 0x7E00
; Shows boot logo, then loads kernel

[BITS 16]
[ORG 0x7E00]

start:
    ; Save drive number from DL
    mov [drive_number], dl
    
    ; Initialize serial
    call serial_init
    
    ; Read memory size from BIOS (0x413 contains KB of base memory)
    xor eax, eax
    mov ax, [0x413]         ; Read KB from BIOS data area
    mov [0x500], ax         ; Store at safe location (0x500) for kernel to read
    
    ; Serial debug message
    mov si, msg_stage2_start
    call serial_print_string
    
    ; Show boot logo
    mov si, msg_before_logo
    call serial_print_string
    
    call show_boot_logo
    
    ; Serial debug after logo
    mov si, msg_stage2_logo_done
    call serial_print_string
    
    ; Load kernel from sector 3 (after MBR + stage2)
    mov si, msg_loading_kernel
    call print_string
    
    mov si, msg_loading_kernel_serial
    call serial_print_string
    
    mov cx, 20              ; Load 20 sectors
    mov ax, 3               ; Start at sector 3
    mov bx, 0x1000
    mov es, bx
    xor bx, bx              ; Target: 0x1000:0000 = physical 0x10000

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

    mov si, msg_ok
    call print_string
    
    mov si, msg_kernel_loaded
    call serial_print_string

    ; Enable A20 Line
    mov si, msg_enabling_a20
    call serial_print_string
    
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Disable interrupts and load GDT
    mov si, msg_loading_gdt
    call serial_print_string
    
    cli
    
    ; Create a temporary GDT descriptor that points to absolute address
    mov eax, gdt_start
    mov ecx, 0x7E00
    add eax, ecx            ; EAX = 0x7E00 + offset of gdt_start
    
    ; Build GDT descriptor on the stack temporarily
    mov word [temp_gdt_descriptor], gdt_end - gdt_start - 1
    mov dword [temp_gdt_descriptor + 2], eax
    
    ; Load GDT
    lgdt [temp_gdt_descriptor]

    mov si, msg_entering_pm
    call serial_print_string
    
    ; Set Protected Mode bit
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit mode
    jmp 0x08:pm_start

; Show boot logo in VGA Mode 13h
show_boot_logo:
    pusha
    
    ; Switch to VGA Mode 13h (320x200, 256 colors)
    mov ax, 0x0013
    int 0x10
    
    ; Fill screen with blue (color 9)
    mov ax, 0xA000
    mov es, ax
    xor di, di
    mov ax, 0x0909          ; Blue in both bytes
    mov cx, 32000           ; 64000 bytes / 2
    rep stosw
    
    ; SHORT delay - 1 second
    mov bp, 3
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
    
    popa
    ret

; Serial port debug functions
serial_init:
    ; Initialize COM1 (0x3F8) - 38400 baud
    mov dx, 0x3F8 + 3       ; Line control register
    mov al, 0x80            ; Enable DLAB
    out dx, al
    
    mov dx, 0x3F8           ; Divisor low byte
    mov al, 0x03            ; 38400 baud
    out dx, al
    
    mov dx, 0x3F8 + 1       ; Divisor high byte
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
    
    ; Send character
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

; Print string to screen
print_string:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .loop
.done:
    popa
    ret

; Read disk sector
; AX = LBA sector, ES:BX = buffer, CX = sector count
disk_read:
    pusha
    push cx
    
    ; Convert LBA to CHS (simplified for hard disk)
    xor dx, dx
    div word [sectors_per_track]
    inc dx
    mov cl, dl              ; Sector
    
    xor dx, dx
    div word [heads]
    mov dh, dl              ; Head
    mov ch, al              ; Cylinder
    
    pop ax                  ; Sector count to AL
    mov dl, [drive_number]
    mov ah, 0x02            ; Read sectors
    int 0x13
    
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

    ; Jump to kernel - simple absolute jump
    jmp 0x10000

; --- Data ---
gdt_start:
    dq 0x0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 0b10011010, 0b11001111, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 0b10010010, 0b11001111, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start            ; Offset within this file = label address

drive_number: db 0x80
sectors_per_track: dw 63
heads: dw 255
temp_gdt_descriptor: dd 0, 0  ; Space for temporary GDT descriptor

msg_stage2: db 'Stage2 loaded', 0x0D, 0x0A, 0
msg_stage2_start: db '[Stage2] Starting...', 0x0D, 0x0A, 0
msg_before_logo: db '[Stage2] Entering show_boot_logo...', 0x0D, 0x0A, 0
msg_stage2_logo_done: db '[Stage2] Logo done, loading kernel...', 0x0D, 0x0A, 0
msg_loading_kernel_serial: db '[Stage2] Loading kernel from sector 3...', 0x0D, 0x0A, 0
msg_kernel_loaded: db '[Stage2] Kernel loaded', 0x0D, 0x0A, 0
msg_enabling_a20: db '[Stage2] Enabling A20 line...', 0x0D, 0x0A, 0
msg_loading_gdt: db '[Stage2] Loading GDT...', 0x0D, 0x0A, 0
msg_entering_pm: db '[Stage2] Entering protected mode...', 0x0D, 0x0A, 0
msg_after_logo: db 'Logo done', 0x0D, 0x0A, 0
msg_loading_kernel: db 'Loading kernel', 0
msg_ok: db ' Done', 0x0D, 0x0A, 0

times 1024-($-$$) db 0      ; Pad to 2 sectors (1024 bytes)
