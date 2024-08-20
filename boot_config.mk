# This file defines constants related to booting that are shared accross the
# build system, bootloader and kernel

BOOT_SECTOR_SIZE := 512
# Number of sectors that the bootloader is made up of
BOOT_SECTOR_COUNT := 16
# Byte offset of the code segment descriptor in the GDT
BOOT_GDT_CODE_DESC := 8
# Byte offset of the data segment descriptor in the GDT
BOOT_GDT_DATA_DESC := 16
# Address of the inital page table on boot
BOOT_PAGE_TAB_ADDR := 0x1000
# Size of a page
BOOT_PAGE_SIZE := 0x1000
# Size of a page table page
BOOT_PAGE_TAB_SIZE := 0x1000

BOOT_MACROS := -D BOOT_SECTOR_SIZE=$(BOOT_SECTOR_SIZE) \
			-D BOOT_SECTOR_COUNT=$(BOOT_SECTOR_COUNT) \
			-D BOOT_GDT_CODE_DESC=$(BOOT_GDT_CODE_DESC) \
			-D BOOT_GDT_DATA_DESC=$(BOOT_GDT_DATA_DESC) \
			-D BOOT_LOAD_ADDR=0x7c00 \
			-D BOOT_PAGE_SIZE=$(BOOT_PAGE_SIZE) \
			-D BOOT_PAGE_TAB_SIZE=$(BOOT_PAGE_TAB_SIZE) \
			-D BOOT_PAGE_TAB_ADDR=$(BOOT_PAGE_TAB_ADDR)
