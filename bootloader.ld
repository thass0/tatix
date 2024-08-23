INCLUDE config.ld

MEMORY
{
    bootloader (rwx) : ORIGIN = BOOT_LOAD_ADDR, LENGTH = SECTOR_SIZE
    load_kernel (rwx) : ORIGIN = BOOT_LOAD_ADDR + SECTOR_SIZE, LENGTH = SECTOR_SIZE * (BOOT_SECTOR_COUNT - 1)
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
    .elf_buf : { *(.elf_buf*); } > load_kernel
    /DISCARD/ :
    {
        *(*);
    }
}
