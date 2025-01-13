INCLUDE config.ld

ENTRY(_start)

SECTIONS
{
    /* The first code that runs in the kernel needs its virtual and physical adddresses to line up */
    . = KERN_ENTRY_PADDR;
    .entry : AT(KERN_ENTRY_PADDR)
    {
        *(*.entry*);
    }

    . = KERN_BASE_VADDR;

    .text : AT(KERN_BASE_PADDR)
    {
        *(.text*);
    }

    .rodata : { *(.rodata*); }

    . = ALIGN(PAGE_SIZE);

    .data :
    {
        *(.data*);
    }

    .init_proc :
    {
        _init_proc_start = .;
        *(.init_proc*);
        _init_proc_end = .;
    }

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
