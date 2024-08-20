MEMORY
{
    bootloader (rwx) : ORIGIN = 0x7c00, LENGTH = 512
    load_kernel (rwx) : ORIGIN = 0x7e00, LENGTH = 0x2000
}

ENTRY(start_real_mode)
SECTIONS
{
    .bootloader : { *(.bootloader*); } > bootloader
    .bootsign (0x7c00 + 510) :
    {
        BYTE(0x55)
        BYTE(0xaa)
    } > bootloader
    .text : { *(.text*); } > load_kernel
    .rodata : { *(.rodata*); } > load_kernel
    /DISCARD/ :
    {
        *(*);
    }
}
