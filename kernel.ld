ENTRY(_start)

SECTIONS
{
    /* The first code that runs in the kernel needs its virtual and physical adddresses to line up */
    . = 0x200000;
    .entry : AT(0x200000)
    {
        *(*.entry*);
    }

    /* Virtual address of the kernel */
    . = 0x80100000;

    /* Physical address of the kernel text */
    .text : AT(0x100000)
    {
        *(.text*);
    }

    .rodata : { *(.rodata*); }

    /* Page align data segment */
    . = ALIGN(0x1000);

    .data : { *(.data*); }
    .bss :
    {
            *(.*bss*);
            *(COMMON);
    }

    /DISCARD/ :
    {
        *(.*eh_frame);
    }
}
