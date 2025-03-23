.PHONY = clean boot fmt

CONFIG := config.mk
include $(CONFIG)

BUILD_DIR := build
BOOTLOADER_DIR := bootloader
SRC_DIR := src

ifeq ($(DEBUG),)
	DEBUG := 0
endif

ifeq ($(shell [ $(DEBUG) -ge 2 ] 2>/dev/null && echo yes), yes)
    DEBUG_FLAGS := -g
endif
ifeq ($(shell [ $(DEBUG) -ge 3 ] 2>/dev/null && echo yes), yes)
    QEMU_DEBUG_FLAGS := -s -S
endif

CC := gcc
CPPFLAGS := -MMD -Iinclude/
CFLAGS := $(DEBUG_FLAGS) -mgeneral-regs-only -std=gnu99 -ffreestanding -mcmodel=large -mno-red-zone -fno-builtin -nostdinc -Wall -Wextra -Wuninitialized -Wmaybe-uninitialized -pedantic

NASM := nasm -f elf64

SRCS := $(wildcard $(SRC_DIR)/*)
OBJS := $(patsubst $(SRC_DIR)/%, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.d, $(filter %.c, $(SRCS)))

ROOTFS_DIR := rootfs/
ROOTFS_OBJ := $(BUILD_DIR)/rootfs.o
ROOTFS_ARCHIVE := $(BUILD_DIR)/rootfs.img

BOOTLOADER_SRCS := $(wildcard $(BOOTLOADER_DIR)/*)
BOOTLOADER_OBJS := $(patsubst $(BOOTLOADER_DIR)/%, $(BUILD_DIR)/%.o, $(BOOTLOADER_SRCS))

BOOTLOADER_IMAGE := $(BUILD_DIR)/bootloader.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
DISK_IMAGE := $(BUILD_DIR)/image.bin

LINKER_CONFIG := $(BUILD_DIR)/config.ld
NASM_CONFIG := $(BUILD_DIR)/config.s
HEADER_CONFIG := $(BUILD_DIR)/config.h

all: $(DISK_IMAGE)

$(DISK_IMAGE): $(BOOTLOADER_IMAGE) $(KERNEL_ELF) | $(BUILD_DIR)
	cat $^ > $@

# Compile bootloader

$(BOOTLOADER_IMAGE): $(BOOTLOADER_OBJS) | $(BUILD_DIR) $(LINKER_CONFIG) bootloader.ld
	ld -L$(dir $(LINKER_CONFIG)) -T bootloader.ld -o $(BUILD_DIR)/bootloader.elf $^
	objcopy -O binary $(BUILD_DIR)/bootloader.elf $@
	truncate -s $(shell expr $(SECTOR_SIZE) \* $(BOOT_SECTOR_COUNT)) $@

$(BUILD_DIR)/%.s.o: $(BOOTLOADER_DIR)/%.s | $(BUILD_DIR) $(NASM_CONFIG)
	$(NASM) -I$(dir $(NASM_CONFIG)) -I$(BOOTLOADER_DIR) $< -o $@

$(BUILD_DIR)/%.c.o: $(BOOTLOADER_DIR)/%.c | $(BUILD_DIR) $(HEADER_CONFIG)
	$(CC) $(CPPFLAGS) -I$(dir $(HEADER_CONFIG)) $(CFLAGS) -c $< -o $@

# Compile kernel

$(KERNEL_ELF): $(OBJS) $(ROOTFS_OBJ) | $(BUILD_DIR) $(LINKER_CONFIG) kernel.ld
	ld -L$(dir $(LINKER_CONFIG)) $(DEBUG_FLAGS) -T kernel.ld -o $@ $^

# To recompile if headers change:
-include $(DEPS)

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c | $(BUILD_DIR) $(HEADER_CONFIG)
	$(CC) $(CPPFLAGS) -D__DEBUG__=$(DEBUG) -D__BASENAME__=\"$(notdir $<)\" -I$(dir $(HEADER_CONFIG)) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.s.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(NASM) $< -o $@

# This will create an archive of the rootfs and place it into the .rootfs_archive section of a normal object.
# The linker will come looking for this later and put the section into the kernel ELF (see kernel.ld).
$(ROOTFS_OBJ): $(ROOTFS_ARCHIVE) | $(BUILD_DIR)
	objcopy -I binary -O elf64-x86-64 -B i386 --rename-section .data=.rootfs_archive,alloc,load,readonly,data,contents $< $@

$(ROOTFS_ARCHIVE): $(ROOTFS_DIR) | $(BUILD_DIR)
	./archive.py enc $< $@

# Misc

$(LINKER_CONFIG): $(CONFIG)
	./make_config.sh -f linker -o $@ $<

$(NASM_CONFIG): $(CONFIG)
	./make_config.sh -f nasm -o $@ $<

$(HEADER_CONFIG): $(CONFIG)
	./make_config.sh -f header -o $@ $<

$(BUILD_DIR):
	mkdir $@

boot: $(DISK_IMAGE)
	qemu-system-x86_64 -m 1G -cpu max -display none -serial stdio -no-reboot -drive file=$<,format=raw,index=0,media=disk $(QEMU_DEBUG_FLAGS)

fmt:
	clang-format -i --style=file $(SRCS) $(wildcard include/*.h)

clean:
	$(RM) -r $(BUILD_DIR)
