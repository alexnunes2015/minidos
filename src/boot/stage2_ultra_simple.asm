[BITS 16]
[ORG 0x7E00]

; Ultra-simple stage2 that just jumps to kernel
; Kernel is loaded at 0x0100:0x0000 (0x1000 physical)

start:
    ; Just jump to kernel in real mode
    ; Kernel entry is at 0x1000
    jmp 0x0100:0x0000
