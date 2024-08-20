.PHONY = clean boot fmt

include boot_config.mk

BUILD_DIR := build
BOOTLOADER_DIR := bootloader
SRC_DIR := src

CC := gcc
CPPFLAGS := -MMD -Iinclude/ $(BOOT_MACROS)
CFLAGS := -std=c99 -ffreestanding -mcmodel=large -mno-red-zone -fno-builtin -nostdinc -Wall -Wextra -pedantic

NASM := nasm -f elf64 $(BOOT_MACROS)

SRCS := $(wildcard $(SRC_DIR)/*)
OBJS := $(patsubst $(SRC_DIR)/%, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.d, $(filter %.c, $(SRCS)))

BOOTLOADER_SRCS := $(wildcard $(BOOTLOADER_DIR)/*)
BOOTLOADER_OBJS := $(patsubst $(BOOTLOADER_DIR)/%, $(BUILD_DIR)/%.o, $(BOOTLOADER_SRCS))

BOOTLOADER_IMAGE := $(BUILD_DIR)/bootloader.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
DISK_IMAGE := $(BUILD_DIR)/image.bin

all: $(DISK_IMAGE)

$(DISK_IMAGE): $(BOOTLOADER_IMAGE) $(KERNEL_ELF) | $(BUILD_DIR)
	cat $^ > $@

# Compile bootloader

$(BOOTLOADER_IMAGE): $(BOOTLOADER_OBJS) | $(BUILD_DIR) bootloader.ld
	ld -T bootloader.ld -o $(BUILD_DIR)/bootloader.elf $^
	objcopy -O binary $(BUILD_DIR)/bootloader.elf $@
	truncate -s $(shell expr $(BOOT_SECTOR_SIZE) \* $(BOOT_SECTOR_COUNT)) $@

$(BUILD_DIR)/%.s.o: $(BOOTLOADER_DIR)/%.s | $(BUILD_DIR)
	$(NASM) -I$(BOOTLOADER_DIR) $< -o $@

$(BUILD_DIR)/%.c.o: $(BOOTLOADER_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Compile kernel

$(KERNEL_ELF): $(OBJS) | $(BUILD_DIR)
	ld -m elf_x86_64 -T kernel.ld -o $@ $^

# To recompile if headers change:
-include $(DEPS)

$(BUILD_DIR)/isr.c.o: $(SRC_DIR)/isr.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -mgeneral-regs-only -c $< -o $@

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.s.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(NASM) $< -o $@

# Misc

$(BUILD_DIR):
	mkdir $@

boot: $(DISK_IMAGE)
	qemu-system-x86_64 -display none -serial stdio -no-reboot -drive file=$<,format=raw,index=0,media=disk

fmt:
	clang-format -i --style=file $(SRCS) $(wildcard include/*.h)

clean:
	$(RM) -r $(BUILD_DIR)
