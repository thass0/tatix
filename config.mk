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
KERN_ENTRY_ADDR=0x100000
# Virtual base address for the kernel
KERN_BASE_VADDR=0x80200000
# Physical base address for the kernel
KERN_BASE_PADDR=0x200000
# Base of dynamically managed memory region
KERN_DATA_VADDR=0x80300000
# Length of dynamically managed memory region
KERN_DATA_LEN=0x200000
KERN_PHYS_PADDR=0x500000
KERN_PHYS_LEN=0x200000
