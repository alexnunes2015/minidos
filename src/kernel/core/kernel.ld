ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* The kernel is loaded at 0x1000:0000 which is physical 0x10000 */
    . = 0x10000;

    .text : {
        *(.text.boot)        /* Entry point FIRST */
        *(.text.startup)
        *(.text)
    } :text

    .rodata : {
        *(.rodata)
    } :text

    .data : {
        *(.data)
    } :data

    .bss : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
