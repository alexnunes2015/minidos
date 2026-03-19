; MiniDOS Bootloader
; Targets BIOS-compatible disk with FAT BPB

[BITS 16]
[ORG 0x7C00]

jmp short start
nop

; FAT BPB (BIOS Parameter Block)
bdb_oem:                    db 'MSWIN4.1'           ; 8 bytes
bdb_bytes_per_sector:       dw 512
bdb_sectors_per_cluster:    db 1
bdb_reserved_sectors:       dw 192
bdb_fat_count:              db 2
bdb_dir_entries_count:      dw 224
bdb_total_sectors:          dw 2880                 ; 2880 * 512 = 1.44MB
bdb_media_descriptor_type:  db 0xF0                 ; F0 = 3.5" floppy
bdb_sectors_per_fat:        dw 9
bdb_sectors_per_track:      dw 18
bdb_heads:                  dw 2
bdb_hidden_sectors:         dd 0
bdb_large_sector_count:     dd 0

; Extended Boot Record
ebr_drive_number:           db 0                    ; 0x00 floppy, 0x80 hdd
                            db 0                    ; reserved
ebr_signature:              db 0x29
ebr_volume_id:              db 0x12, 0x34, 0x56, 0x78
ebr_volume_label:           db 'MINIDOS    '        ; 11 bytes
ebr_system_id:              db 'FAT12   '           ; 8 bytes

start:
    ; SAVE BOOT DRIVE
    mov [ebr_drive_number], dl

    ; Setup segments correctly
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Print "MiniDOS v0.1"
    mov si, msg_boot
    call print_string

    ; Load Stage2 bootloader from sector 1 (2048 bytes = 4 sectors)
    mov cx, 4               ; Load 4 sectors
    mov ax, 1               ; Start at sector 1
    mov bx, 0x0000
    mov es, bx
    mov bx, 0x7E00          ; Target: 0x0000:0x7E00

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

    ; Pass drive number to stage2 via DL
    mov dl, [ebr_drive_number]
    
    ; Jump to second-stage bootloader at 0x0000:0x7E00
    jmp 0x0000:0x7E00

[BITS 16]
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

disk_read:
    push ax
    push bx
    push cx
    push dx
    push di

    push cx                 
    call lba_to_chs         
    pop ax                  
    
    mov ah, 0x02            
    mov di, 3               
.retry:
    pusha
    int 0x13
    jnc .success
    popa
    
    pusha
    xor ax, ax
    int 0x13
    popa
    
    dec di
    jnz .retry
    
    jmp $

.success:
    popa
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

lba_to_chs:
    ; ax = LBA
    push ax
    push dx

    xor dx, dx
    div word [bdb_sectors_per_track]
    inc dx
    mov cl, dl          ; Sector

    xor dx, dx
    div word [bdb_heads]
    mov dh, dl          ; Head
    mov ch, al          ; Cylinder
    shl ah, 6
    or cl, ah

    pop dx
    pop ax
    mov dl, [ebr_drive_number]
    ret

; --- GDT Definition ---
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
    dd gdt_start

msg_boot: db 'MiniDOS v0.1 loading...', 0
msg_ok:   db ' OK', 0x0D, 0x0A, 0

times 446-($-$$) db 0       ; Pad unused area in floppy boot sector
; Superfloppy layout: no partition table is used
times 510-($-$$) db 0       ; Pad to 510
dw 0xAA55                   ; Boot signature
