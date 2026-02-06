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

    ; Detect INT 13h extensions (LBA). Skip for floppy drives.
    mov byte [lba_supported], 0
    mov dl, [drive_number]
    cmp dl, 0x80
    jb .lba_check_done
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .lba_check_done
    cmp bx, 0xAA55
    jne .lba_check_done
    test cx, 0x0001
    jz .lba_check_done
    mov byte [lba_supported], 1
.lba_check_done:

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
    mov byte [geom_ok], 1
.geom_done:
    ; Fallback geometry for floppy if BIOS query failed
    cmp byte [geom_ok], 0
    jne .geom_done2
    cmp byte [drive_number], 0x80
    jae .geom_done2
    mov word [sectors_per_track], 18
    mov word [heads], 2
.geom_done2:
    
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
    
    ; Load kernel from sector 5 (after MBR + stage2)
    mov si, msg_loading_kernel
    call serial_print_string
    
    mov cx, 20              ; Load 20 sectors
    mov ax, 5               ; Start at sector 5
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

    ; Pick blue/white indices from the loaded palette for the bar
    call pick_bar_colors
    
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

    ; Animate a 5px bottom blue/white gradient bar for a fixed 5 seconds
    ; BIOS tick at 0x046C increments ~18.2 times per second
    mov bx, [0x046C]        ; start tick
    mov [bar_start_tick], bx
    mov [bar_last_tick], bx
    mov word [bar_frame], 0 ; frame offset (0..319)
.frame_wait:
    mov ax, [0x046C]
    cmp ax, [bar_last_tick]
    je .frame_wait          ; wait for next tick
    mov [bar_last_tick], ax ; update last tick

    ; Check elapsed ticks (5 seconds ≈ 91 ticks)
    mov cx, ax
    sub cx, [bar_start_tick]
    cmp cx, 91
    jae .frame_done

    ; Compute gradient start for this frame
    mov ax, [bar_frame]
    mov bl, al
    and bl, 0x07
    mov [bar_xmod_start], bl
    xor dx, dx
    mov bx, 5
    div bx                  ; ax = frame/5, dx = frame%5
    mov [bar_intensity_start], al
    mov [bar_step_start], dl

    ; Draw 5 rows at y=195..199 (320x200)
    xor bp, bp              ; row = 0
.bar_row:
    mov di, 62400           ; 195 * 320
    mov ax, bp
    mov cx, 320
    mul cx                  ; ax = row * 320
    add di, ax

    mov si, bayer8x8
    mov ax, bp
    and ax, 0x0007
    shl ax, 3               ; row_base = (row & 7) * 8
    add si, ax              ; SI = row pointer

    mov al, [bar_intensity_start]
    mov ah, [bar_step_start]
    xor bx, bx
    mov bl, [bar_xmod_start]
    mov cx, 320
.bar_col:
    mov dl, [si + bx]       ; bayer value 0..63
    cmp dl, al
    jb .bar_white
    mov dl, [bar_blue_idx]
    jmp .bar_store
.bar_white:
    mov dl, [bar_white_idx]
.bar_store:
    mov [es:di], dl
    inc di

    inc bl
    and bl, 0x07
    inc ah
    cmp ah, 5
    jb .bar_next
    xor ah, ah
    inc al
    and al, 0x3F
.bar_next:
    loop .bar_col

    inc bp
    cmp bp, 5
    jb .bar_row

    ; Advance frame offset (wrap at 320)
    mov ax, [bar_frame]
    inc ax
    cmp ax, 320
    jb .bar_frame_store
    xor ax, ax
.bar_frame_store:
    mov [bar_frame], ax
    jmp .frame_wait
.frame_done:
    
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

; Pick palette indices for blue and white (from loaded palette at PAL_BUFFER)
pick_bar_colors:
    pusha
    push ds
    xor ax, ax
    mov ds, ax

    mov si, PAL_BUFFER
    xor bx, bx              ; index
    mov byte [bar_white_score], 0
    mov byte [bar_white_idx], 0
    mov byte [bar_blue_score], 0
    mov byte [bar_blue_idx], 0
.color_loop:
    mov al, [si]            ; R
    mov ah, [si + 1]        ; G
    mov dl, [si + 2]        ; B

    mov dh, al              ; sum = R+G+B
    add dh, ah
    add dh, dl
    cmp dh, [bar_white_score]
    jbe .check_blue
    mov [bar_white_score], dh
    mov [bar_white_idx], bl
.check_blue:
    mov dh, dl              ; score = (B*2) + 128 - R - G
    add dh, dl
    add dh, 128
    sub dh, al
    sub dh, ah
    cmp dh, [bar_blue_score]
    jbe .next_color
    mov [bar_blue_score], dh
    mov [bar_blue_idx], bl
.next_color:
    add si, 3
    inc bl
    jnz .color_loop

    pop ds
    popa
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

    cmp byte [lba_supported], 0
    je .chs_fallback

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

    ; LBA failed (likely floppy or unsupported) -> disable and fallback to CHS
    mov byte [lba_supported], 0

    ; CHS fallback
.chs_fallback:
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
lba_supported: db 0
geom_ok: db 0
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

bar_frame: db 0, 0
bar_start_tick: db 0, 0
bar_last_tick: db 0, 0
bar_white_idx: db 0
bar_blue_idx: db 0
bar_white_score: db 0
bar_blue_score: db 0
bar_intensity_start: db 0
bar_step_start: db 0
bar_xmod_start: db 0

bayer8x8:
    db 0, 48, 12, 60, 3, 51, 15, 63
    db 32, 16, 44, 28, 35, 19, 47, 31
    db 8, 56, 4, 52, 11, 59, 7, 55
    db 40, 24, 36, 20, 43, 27, 39, 23
    db 2, 50, 14, 62, 1, 49, 13, 61
    db 34, 18, 46, 30, 33, 17, 45, 29
    db 10, 58, 6, 54, 9, 57, 5, 53
    db 42, 26, 38, 22, 41, 25, 37, 21

times 2048-($-$$) db 0      ; Pad to 2048 bytes
