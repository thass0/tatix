.PHONY = clean boot fmt

BUILD_DIR := build
BOOTLOADER_DIR := bootloader
SRC_DIR := src

CC := gcc
CPPFLAGS := -MMD -Iinclude/
CFLAGS := -std=c99 -ffreestanding -m64 -mno-red-zone -fno-builtin -nostdinc -Wall -Wextra -pedantic

NASM := nasm -f elf64

C_SRCS := $(wildcard $(SRC_DIR)/*.c)
C_OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS))
C_DEPS := $(C_OBJS:%.o=%.d)

BOOTLOADER_SRCS := $(BOOTLOADER_DIR)/stage1.s $(BOOTLOADER_DIR)/stage2.s
BOOTLOADER_OBJS := $(patsubst $(BOOTLOADER_DIR)/%.s, $(BUILD_DIR)/%.o, $(BOOTLOADER_SRCS))

ASM_SRCS := $(wildcard $(SRC_DIR)/*.s)
ASM_OBJS := $(patsubst $(SRC_DIR)/%.s, $(BUILD_DIR)/%.o, $(ASM_SRCS))

OBJS := $(C_OBJS) $(ASM_OBJS) $(BOOTLOADER_OBJS)

BOOT_IMAGE := $(BUILD_DIR)/image.bin

$(BOOT_IMAGE): $(BUILD_DIR)/kernel.o
	objcopy -O binary $< $@

$(BUILD_DIR)/kernel.o: $(OBJS) | $(BUILD_DIR)
	ld -T linker.ld -o $@ $^

$(BUILD_DIR)/%.o: $(BOOTLOADER_DIR)/%.s | $(BUILD_DIR)
	$(NASM) -I$(BOOTLOADER_DIR) $< -o $@

# To re-compile if headers change:
-include $(C_DEPS)

$(BUILD_DIR)/isr.o: $(SRC_DIR)/isr.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -mgeneral-regs-only -c $< -o $@

$(BUILD_DIR)/vga.o: $(SRC_DIR)/vga.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -mgeneral-regs-only -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)
	$(NASM) $< $@

$(BUILD_DIR):
	mkdir $@

boot: $(BOOT_IMAGE)
	qemu-system-x86_64 -no-reboot -drive file=$<,format=raw,index=0,media=disk

fmt:
	clang-format -i --style=file $(SRCS) $(wildcard include/*.h)

clean:
	$(RM) -r $(BUILD_DIR)
