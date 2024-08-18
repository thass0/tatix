MEMORY
{
        bootloader (rwx) : ORIGIN = 0x7c00, LENGTH = 512
        kernel (rwx) : ORIGIN = 0x7e00, LENGTH = 65024
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
        .text : { *(.text*); } > kernel
        .rodata : { *(.rodata*); } > kernel
        .data : { *(.data*); } > kernel
        .bss :
        {
                *(.bss*)
                *(COMMON)
        } > kernel
}
