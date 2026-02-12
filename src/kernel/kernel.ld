ENTRY(_start)

SECTIONS
{
    /* The kernel is loaded at 0x1000:0000 which is physical 0x10000 */
    . = 0x10000;

    .text : {
        *(.text.boot)        /* Entry point FIRST */
        *(.text.startup)
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
