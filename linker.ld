MEMORY
{
        stage1 (rwx) : ORIGIN = 0x7c00, LENGTH = 512
        stage2 (rwx) : ORIGIN = 0x7e00, LENGTH = 512
        kernel (rwx) : ORIGIN = 0x8000, LENGTH = 32768
}

ENTRY(boot_stage1)
SECTIONS
{
        .stage1 : { *(.stage1*); } > stage1
        .bootsign (0x7c00 + 510) :
        {
                BYTE(0x55)
                BYTE(0xaa)
        } > stage1
        .stage2 : { *(.stage2*); } > stage2
        .text : { *(.text*); } > kernel
        .rodata : { *(.rodata*); } > kernel
        .data : { *(.data*); } > kernel
        .bss :
        {
                *(.bss*)
                *(COMMON)
        } > kernel
}
