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
KERN_BASE_PADDR=0x120000
# Virtual base address for kernel code and static data (excluding .entry)
KERN_BASE_VADDR=0x80120000

# NOTE: There are two regions of dynamically managed memory now. The first is
# `KERN_DATA_VADDR`. All page tables currently in use map the first few MiB
# startig at virtual address `KERN_BASE_VADDR` to the physical addresses starting
# at `KERN_BASE_PADDR`. This means the region of size `KERN_DATA_LEN` that begins
# at `KERN_DATA_VADDR` is so mapped and sits at physical addresses
#
# 	KERN_DATA_PADDR = KERN_DATA_VADDR - KERN_BASE_VADDR + KERN_BASE_PADDR
#
# `KERN_BASE_VADDR` is the virtual memory regino currently being used for kernel
# data structures.
# Base of dynamically managed memory region
KERN_DATA_VADDR=0x80300000
# Length of dynamically managed memory region
KERN_DATA_LEN=0x200000

# This is the region of physical memory that's currently being used to back up
# the physical page allocator.
KERN_PHYS_PADDR=0x500000
KERN_PHYS_LEN=0x200000

# PLAN:
# Write a proper kernel allocator that hands out virtual addresses for kernel
# data structures. The kernel allocator gets its backing memory from a single
# physical allocator that's used by the entire system. This physical allocator
# will manage all the physical memory that comes after the region where the
# kernel code and static data live now.
# In this file, `KERN_DATA_VADDR` and `KERN_DATA_LEN` will be deleted and
# new variables `KERN_DYN_PADDR` and `KERN_DYN_LEN` will be created. They
# will define the region of physical memory that the physical allocator manages.
#
# In the future of course, the BIOS/UEFI memory map should be considered to
# make these choices but for simplicity I'm hard coding these values in here.
