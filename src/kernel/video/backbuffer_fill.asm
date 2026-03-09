global video_backbuffer_fill_rect32

extern video_backbuffer_fill_base
extern video_backbuffer_fill_pitch
extern video_backbuffer_fill_h
extern video_backbuffer_fill_w
extern video_backbuffer_fill_rgb

section .text

video_backbuffer_fill_rect32:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov edx, [video_backbuffer_fill_base]
    mov esi, [video_backbuffer_fill_pitch]
    mov ebx, [video_backbuffer_fill_h]
    mov ecx, [video_backbuffer_fill_w]
    mov eax, [video_backbuffer_fill_rgb]

    test ebx, ebx
    jle .done
    test ecx, ecx
    jle .done

.row_loop:
    mov edi, edx
    push ebx
    mov ebx, ecx

.quad_loop:
    cmp ebx, 4
    jb .tail_loop
    mov [edi], eax
    mov [edi + 4], eax
    mov [edi + 8], eax
    mov [edi + 12], eax
    add edi, 16
    sub ebx, 4
    jmp .quad_loop

.tail_loop:
    test ebx, ebx
    jz .next_row
    mov [edi], eax
    add edi, 4
    dec ebx
    jmp .tail_loop

.next_row:
    pop ebx
    add edx, esi
    dec ebx
    jnz .row_loop

.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
