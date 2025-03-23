INCLUDE config.ld

ENTRY(_start)

PHDRS
{
    entry_seg PT_LOAD FLAGS(7); /* rwx */
    text_seg PT_LOAD FLAGS(5); /* r-x */
    rodata_seg PT_LOAD FLAGS(4); /* r-- */
    data_seg PT_LOAD FLAGS(6); /* rw- */
    extra_seg PT_LOAD FLAGS(4); /* r-- */
}

SECTIONS
{
    /* The first code that runs in the kernel needs its virtual and physical adddresses to line up */
    . = KERN_ENTRY_PADDR;
    .entry : AT(KERN_ENTRY_PADDR)
    {
        *(*.entry*);
    } :entry_seg

    . = KERN_BASE_VADDR;

    .text : AT(KERN_BASE_PADDR)
    {
        *(.text*);
    } :text_seg

    .rodata : { *(.rodata*); } :rodata_seg

    . = ALIGN(PAGE_SIZE);

    .data :
    {
        *(.data*);
    } :data_seg

    .bss :
    {
            *(.*bss*);
            *(COMMON);
    } :data_seg

    .init_proc :
    {
        /* Symbol for the C side to find that data */
        _init_proc_start = .;
        *(.init_proc*);
        _init_proc_end = .;
    } :extra_seg

    .rootfs_archive :
    {
        /* Symbol for the C side to find that data */
        _rootfs_archive_start = .;
        *(.rootfs_archive*);
        _rootfs_archive_end = .;
    } :extra_seg

    /DISCARD/ :
    {
        *(.*eh_frame);
    }
}
