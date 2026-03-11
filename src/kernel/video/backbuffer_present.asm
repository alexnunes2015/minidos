global video_backbuffer_present_rect32
global video_backbuffer_present_rect24

extern video_backbuffer_present_src
extern video_backbuffer_present_dst
extern video_backbuffer_present_src_pitch
extern video_backbuffer_present_dst_pitch
extern video_backbuffer_present_w
extern video_backbuffer_present_h

section .text

; void video_backbuffer_present_rect32(void)
;
; Fast copy from backbuffer to frontbuffer using rep movsd.
; Parameters passed via global variables:
;   video_backbuffer_present_src       - source (backbuffer) pointer
;   video_backbuffer_present_dst       - destination (frontbuffer) pointer
;   video_backbuffer_present_src_pitch - source pitch (bytes per row)
;   video_backbuffer_present_dst_pitch - destination pitch (bytes per row)
;   video_backbuffer_present_w         - width in pixels (dwords)
;   video_backbuffer_present_h         - height in rows

video_backbuffer_present_rect32:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [video_backbuffer_present_h]
    test eax, eax
    jle .done32

    mov ebx, [video_backbuffer_present_w]
    test ebx, ebx
    jle .done32

    sub esp, 8
    mov edx, [video_backbuffer_present_src]
    mov [esp], edx
    mov edx, [video_backbuffer_present_dst]
    mov [esp + 4], edx

    cld

.row32:
    mov esi, [esp]
    mov edi, [esp + 4]
    mov ecx, ebx
    rep movsd

    mov edx, [video_backbuffer_present_src_pitch]
    add [esp], edx
    mov edx, [video_backbuffer_present_dst_pitch]
    add [esp + 4], edx

    dec eax
    jnz .row32

    add esp, 8

.done32:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; void video_backbuffer_present_rect24(void)
;
; Convert 32-bit backbuffer pixels (0x00RRGGBB) to 24-bit framebuffer
; pixels (BB GG RR, 3 bytes each).  In little-endian memory the first
; 3 bytes of any u32 0x00RRGGBB are already BB GG RR, so we just need
; to copy 3 bytes per pixel, skipping the 4th.

video_backbuffer_present_rect24:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, [video_backbuffer_present_h]
    test eax, eax
    jle .done24

    mov ebx, [video_backbuffer_present_w]
    test ebx, ebx
    jle .done24

    sub esp, 8
    mov edx, [video_backbuffer_present_src]
    mov [esp], edx
    mov edx, [video_backbuffer_present_dst]
    mov [esp + 4], edx

.row24:
    mov esi, [esp]
    mov edi, [esp + 4]
    mov ecx, ebx

.pixel24:
    mov edx, [esi]          ; read 32-bit pixel  (BB GG RR 00)
    mov [edi], dx            ; write BB GG  (16-bit store)
    shr edx, 16
    mov [edi + 2], dl        ; write RR
    add esi, 4
    add edi, 3
    dec ecx
    jnz .pixel24

    mov edx, [video_backbuffer_present_src_pitch]
    add [esp], edx
    mov edx, [video_backbuffer_present_dst_pitch]
    add [esp + 4], edx

    dec eax
    jnz .row24

    add esp, 8

.done24:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
