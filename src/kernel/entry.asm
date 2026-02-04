; Kernel Entry Point
; This is the first code executed when the bootloader jumps to 0x10000

[BITS 32]
[EXTERN kernel_main]
[GLOBAL _start]

section .text.boot
_start:
    ; Ensure we have a valid stack
    mov esp, 0x90000
    mov ebp, esp
    
    ; Clear direction flag
    cld
    
    ; Call the C kernel main function
    call kernel_main
    
    ; If kernel_main returns (it shouldn't), halt
.hang:
    cli
    hlt
    jmp .hang
