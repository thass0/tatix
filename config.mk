SECTOR_SIZE=512
PAGE_SIZE=0x1000
# Number of sectors that the bootloader is made up of
BOOT_SECTOR_COUNT=16
# Byte offset of the code segment descriptor in the GDT
BOOT_GDT_CODE_DESC=8
# Byte offset of the data segment descriptor in the GDT
BOOT_GDT_DATA_DESC=16
# Address of the inital page table on boot
BOOT_PAGE_TAB_ADDR=0x1000
# Address where to bootloader is loaded into memory by BIOS
BOOT_LOAD_ADDR=0x7c00
# Physical address where the .entry section of the kernel is loaded
KERN_ENTRY_PADDR=0x100000

# Physical base address for kernel code and static data (excluding .entry)
KERN_BASE_PADDR=0x130000
# Virtual base address for kernel code and static data (excluding .entry)
KERN_BASE_VADDR=0x80130000

# Physical base address for kernel dynamic data
KERN_DYN_PADDR=0x1000000
# Virtual base address for kernel dynamic data
KERN_DYN_VADDR=0x81000000
# Length of the kernel dynamic data section
KERN_DYN_LEN=0x3fe00000
