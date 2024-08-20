ENTRY(_start)

SECTIONS
{
    /* This is the virtual address where the kernel starts in virtual memory */
    . = 0x80100000;

    /* The physical load address of the kernel is lower than its virtual address */
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
            *(.bss*);
            *(COMMON);
    }

}
