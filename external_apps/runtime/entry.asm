[BITS 32]
[GLOBAL _start]
[EXTERN app_main]

section .text.boot
_start:
    mov eax, [esp + 4]
    push eax
    call app_main
    add esp, 4
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
