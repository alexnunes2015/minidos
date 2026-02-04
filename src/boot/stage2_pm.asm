[BITS 16]
[ORG 0x7E00]

; Stage2: Initialize protected mode and jump to kernel
; Kernel is loaded at 0x1000 (0x0100:0000 in real mode addressing)

start:
    ; Initialize serial first (in real mode)
    call init_serial
    mov si, msg_stage2_started
    call send_serial_string
    
    ; Load GDT
    lgdt [gdt_descriptor]
    
    ; Enable A20 line
    call enable_a20
    
    mov si, msg_entering_pm
    call send_serial_string
    
    ; Disable interrupts
    cli
    
    ; Enable Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Now we're in protected mode - jump to kernel
    ; Use a far jump with explicit addressing  
    ; In protected mode, selector:offset addressing still works
    ; 0x08 is our code selector, 0x1000 is the offset
    jmp 0x08:0x1000


init_serial:
    mov dx, 0x3F8 + 3  ; Line control register
    mov al, 0x80       ; Enable DLAB
    out dx, al
    
    mov dx, 0x3F8     ; Divisor low byte  
    mov al, 3          ; 38400 baud
    out dx, al
    
    mov dx, 0x3F8 + 1 ; Divisor high byte
    xor al, al
    out dx, al
    
    mov dx, 0x3F8 + 3 ; Line control
    mov al, 0x03       ; 8N1
    out dx, al
    ret

send_serial_string:
    ; SI = string
.loop:
    lodsb
    or al, al
    jz .done
    
    mov dx, 0x3F8 + 5  ; Line status
.wait:
    in al, dx
    test al, 0x20      ; TX empty?
    jz .wait
    
    mov al, [si - 1]
    mov dx, 0x3F8
    out dx, al
    jmp .loop
.done:
    ret

enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; GDT
gdt_start:
    dq 0x0              ; Null descriptor
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 0b10011010, 0b11001111, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 0b10010010, 0b11001111, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0x7E00 + gdt_start   ; Absolute address in memory (0x7E00 + offset)

msg_stage2_started: db '[Stage2] Started', 0x0D, 0x0A, 0
msg_entering_pm: db '[Stage2] Entering PM, jumping to kernel', 0x0D, 0x0A, 0

times 1024-($-$$) db 0  ; Pad to 1024 bytes
