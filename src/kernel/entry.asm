; Kernel Entry Point
; This is the first code executed when the bootloader jumps to 0x10000

[BITS 32]
%define KERNEL_LOAD_BASE    0x10000
%define PM_CODE_SEL         0x08
%define PM_DATA_SEL         0x10
%define PM16_CODE_SEL       0x18
%define PM16_DATA_SEL       0x20
%define REALMODE_CODE_SEG   0x1000
%define REALMODE_STACK_TOP  0x6000
%define CR0_PE              0x00000001
%define CR0_PG              0x80000000

[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]
[GLOBAL _start]
[GLOBAL biosdisk_boot_read_sector]
[GLOBAL biosdisk_boot_write_sector]
[GLOBAL biosdisk_transfer_buffer]

section .text.boot
_start:
    cli

    ; Replace the minimal boot GDT with one that also includes 16-bit selectors.
    lgdt [thunk_gdt_desc]

    mov ax, PM_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Ensure we have a valid stack
    mov esp, 0x90000
    mov ebp, esp
    
    ; Clear direction flag
    cld

    ; The flat kernel binary omits .bss, so zero it explicitly.
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    mov edx, ecx
    shr ecx, 2
    rep stosd
    mov ecx, edx
    and ecx, 3
    rep stosb
    
    ; Call the C kernel main function
    call kernel_main
    
    ; If kernel_main returns (it shouldn't), halt
.hang:
    cli
    hlt
    jmp .hang

biosdisk_boot_read_sector:
    mov byte [biosdisk_rw_command], 0x02
    jmp biosdisk_boot_rw_common

biosdisk_boot_write_sector:
    mov byte [biosdisk_rw_command], 0x03

biosdisk_boot_rw_common:
    pushfd
    pop eax
    mov [biosdisk_saved_eflags], eax
    sidt [biosdisk_saved_idtr]
    mov [biosdisk_saved_ebx], ebx
    mov [biosdisk_saved_esi], esi
    mov [biosdisk_saved_edi], edi
    mov [biosdisk_saved_ebp], ebp
    mov [biosdisk_saved_esp], esp
    mov byte [biosdisk_status], 0xFF

    mov eax, [esp + 4]
    mov [biosdisk_arg_drive], al
    mov eax, [esp + 8]
    mov [biosdisk_arg_cylinder], ax
    mov eax, [esp + 12]
    mov [biosdisk_arg_head], al
    mov eax, [esp + 16]
    mov [biosdisk_arg_sector], al

    mov eax, biosdisk_transfer_buffer
    mov edx, eax
    and eax, 0x0000000F
    mov [biosdisk_buffer_offset], ax
    shr edx, 4
    mov [biosdisk_buffer_segment], dx

    cli
    jmp word PM16_CODE_SEL:(biosdisk_pm16_entry - _start)

[BITS 16]
biosdisk_pm16_entry:
    mov ax, PM16_DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, biosdisk_pm_stack_top - _start

    mov eax, cr0
    and eax, 0x7FFFFFFF
    mov cr0, eax
    jmp word PM16_CODE_SEL:(biosdisk_pm16_no_paging - _start)

biosdisk_pm16_no_paging:
    mov ax, PM16_DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, biosdisk_pm_stack_top - _start

    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp word REALMODE_CODE_SEG:(biosdisk_real_mode_entry - _start)

biosdisk_real_mode_entry:
    xor ax, ax
    mov ss, ax
    mov sp, REALMODE_STACK_TOP
    cld

    mov ax, REALMODE_CODE_SEG
    mov ds, ax
    lidt [biosdisk_realmode_idtr - _start]

    call biosdisk_pic_to_bios

    mov bx, [biosdisk_buffer_offset - _start]
    mov ax, [biosdisk_buffer_segment - _start]
    mov es, ax

    mov dl, [biosdisk_arg_drive - _start]
    mov dh, [biosdisk_arg_head - _start]
    mov cl, [biosdisk_arg_sector - _start]
    mov ax, [biosdisk_arg_cylinder - _start]
    mov ch, al
    mov al, ah
    and al, 0x03
    shl al, 6
    or cl, al

    mov ah, 0x00
    sti
    int 0x13
    cli

    mov al, 1
    mov ah, [biosdisk_rw_command - _start]
    sti
    int 0x13
    cli

    mov ax, REALMODE_CODE_SEG
    mov ds, ax
    jc .io_failed
    mov byte [biosdisk_status - _start], 0
    jmp .io_done

.io_failed:
    mov byte [biosdisk_status - _start], ah

.io_done:
    call biosdisk_pic_to_kernel

    cli
    lgdt [thunk_gdt_desc - _start]
    mov eax, cr0
    or eax, CR0_PE
    mov cr0, eax
    jmp dword PM_CODE_SEL:biosdisk_pm32_no_paging

biosdisk_pic_to_bios:
    in al, 0x21
    mov [biosdisk_pic_mask_master - _start], al
    in al, 0xA1
    mov [biosdisk_pic_mask_slave - _start], al

    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    mov al, 0x08
    out 0x21, al
    mov al, 0x70
    out 0xA1, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0x21, al
    out 0xA1, al

    mov al, 0xBF
    out 0x21, al
    mov al, 0xFF
    out 0xA1, al
    ret

biosdisk_pic_to_kernel:
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0x21, al
    out 0xA1, al

    mov al, [biosdisk_pic_mask_master - _start]
    out 0x21, al
    mov al, [biosdisk_pic_mask_slave - _start]
    out 0xA1, al
    ret

[BITS 32]
biosdisk_pm32_no_paging:
    mov ax, PM_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov eax, cr0
    or eax, CR0_PG
    mov cr0, eax
    jmp PM_CODE_SEL:biosdisk_pm32_paged

biosdisk_pm32_paged:
    mov ax, PM_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    lidt [biosdisk_saved_idtr]

    mov esp, [biosdisk_saved_esp]
    mov ebx, [biosdisk_saved_ebx]
    mov esi, [biosdisk_saved_esi]
    mov edi, [biosdisk_saved_edi]
    mov ebp, [biosdisk_saved_ebp]

    xor eax, eax
    cmp byte [biosdisk_status], 0
    je .restore_interrupts
    mov eax, -1

.restore_interrupts:
    test dword [biosdisk_saved_eflags], 0x00000200
    jz .return_to_caller
    sti

.return_to_caller:
    ret

align 512
biosdisk_transfer_buffer:
    times 512 db 0

align 4
biosdisk_saved_esp:        dd 0
biosdisk_saved_ebx:        dd 0
biosdisk_saved_esi:        dd 0
biosdisk_saved_edi:        dd 0
biosdisk_saved_ebp:        dd 0
biosdisk_saved_eflags:     dd 0
biosdisk_saved_idtr:       dw 0
                          dd 0
biosdisk_arg_drive:        db 0
biosdisk_arg_head:         db 0
biosdisk_arg_sector:       db 0
biosdisk_rw_command:       db 0
biosdisk_status:           db 0
                           db 0
biosdisk_arg_cylinder:     dw 0
biosdisk_buffer_offset:    dw 0
biosdisk_buffer_segment:   dw 0
biosdisk_pic_mask_master:  db 0
biosdisk_pic_mask_slave:   db 0
                           db 0
                           db 0

align 16
biosdisk_pm_stack:
    times 256 db 0
biosdisk_pm_stack_top:

align 8
thunk_gdt_start:
    dq 0x0000000000000000
thunk_gdt_code32:
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
thunk_gdt_data32:
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
thunk_gdt_code16:
    dw 0xFFFF, 0x0000
    db 0x01, 0x9A, 0x00, 0x00
thunk_gdt_data16:
    dw 0xFFFF, 0x0000
    db 0x01, 0x92, 0x00, 0x00
thunk_gdt_end:

thunk_gdt_desc:
    dw thunk_gdt_end - thunk_gdt_start - 1
    dd thunk_gdt_start

biosdisk_realmode_idtr:
    dw 0x03FF
    dd 0x00000000
