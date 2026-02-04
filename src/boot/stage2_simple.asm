; MiniDOS Simple Second-Stage Bootloader
; Loaded by MBR at 0x7E00
; Just loads kernel and jumps to it

[BITS 16]
[ORG 0x7E00]

start:
    ; Save drive number from DL
    mov [drive_number], dl
    
    ; Initialize serial
    call serial_init
    
    mov si, msg_start
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

; Read disk sector
; AX = LBA sector, CX = 1
disk_read:
    pusha
    
    ; Convert LBA to CHS
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

msg_start: db '[Stage2] Started', 0x0D, 0x0A, 0
msg_loading_kernel: db '[Stage2] Loading kernel...', 0x0D, 0x0A, 0
msg_loaded: db '[Stage2] Kernel loaded', 0x0D, 0x0A, 0
msg_pm: db '[Stage2] Entering PM...', 0x0D, 0x0A, 0
msg_before_jump: db '[Stage2] Before far jump', 0x0D, 0x0A, 0

times 1024-($-$$) db 0      ; Pad to 1024 bytes
