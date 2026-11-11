/* Der Kernel wird als flaches Binärimage nach 0x10000 geladen. */
ENTRY(_start)

SECTIONS
{
    . = 0x10000;
    __kernel_start = .;

    .text : {
        *(.text.entry)              /* _start muss ganz vorne stehen */
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
    }
}
