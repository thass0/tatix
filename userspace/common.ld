/* INCLUDE config.ld */

ENTRY(_start)

PHDRS
{
    text_seg PT_LOAD FLAGS(5); /* r-x */
    rodata_seg PT_LOAD FLAGS(4); /* r-- */
    data_seg PT_LOAD FLAGS(6); /* rw- */
}

SECTIONS
{
    . = 0x400000; /* This is the base virtual address where ELFs are loaded */
    .text   : { *(.text*); } :text_seg
    .rodata : { *(.rodata*); } :rodata_seg
    .data   : { *(.data*); } :data_seg
    .bss    : { *(.bss*); *(COMMON); } :data_seg

    /DISCARD/ :
    {
        *(.*eh_frame*);
        *(.note*);
    }
}
