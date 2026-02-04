[BITS 16]
[ORG 0x7E00]

; Minimal Stage2 - just initialize PM and loop

start:
    ; Enable A20
    in al, 0x92
    or al, 2
    out 0x92, al
    
    ; Load GDT
    lgdt [gdt_descriptor]
    
    ; Enable Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Now in 32-bit mode - make a far jump to ensure prefixes are correct
    ; But use code selector 8
    db 0xEA                ; Opcode for far JMP
    dd 0x00001000        ; Offset = 0x1000 (32-bit)
    dw 0x0008            ; Selector = 8

; Data
align 4
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
    dd gdt_start + 0x7E00

times 1024-($-$$) db 0
