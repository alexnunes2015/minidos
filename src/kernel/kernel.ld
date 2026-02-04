ENTRY(_start)

SECTIONS
{
    /* The kernel will be loaded at 0x0100:0000 which is physical 0x1000 */
    . = 0x1000;

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
