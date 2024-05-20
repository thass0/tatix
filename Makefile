.PHONY = clean boot fmt

# Configure how many sectors the bootloader should read from the boot disk
READ_SECTORS_NUM ?= 127

BUILD_DIR := build
BOOTLOADER_DIR := bootloader
SRC_DIR := src

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:%.o=%.d)

CC := gcc
QEMU := qemu-system-x86_64 -no-reboot
NASM := nasm -dSECTOR_SIZE=512 -dBOOT_LOAD_ADDR=0x7c00 -I$(BOOTLOADER_DIR)

CPPFLAGS := -MMD -Iinclude/
CFLAGS := -std=c99 -ffreestanding -m64 -fno-builtin -nostdinc

BOOT_IMAGE := $(BUILD_DIR)/image.bin

$(BOOT_IMAGE): $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin
	cat $^ > $@

$(BUILD_DIR)/stage1.bin: $(BOOTLOADER_DIR)/stage1.s | $(BUILD_DIR)
	$(NASM) -dREAD_SECTORS_NUM=$(READ_SECTORS_NUM) $< -f bin -o $@

$(BUILD_DIR)/stage2.bin: $(BOOTLOADER_DIR)/stage2.s | $(BUILD_DIR)
	$(NASM) $< -f bin -o $@

$(BUILD_DIR)/kernel.bin: $(OBJS) | $(BUILD_DIR)
	ld -Ttext 0x8000 -o $(BUILD_DIR)/kernel.elf $(OBJS)
	objcopy -O binary $(BUILD_DIR)/kernel.elf $@

# To re-compile if headers change:
-include $(DEPS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir $@

boot: $(BOOT_IMAGE)
	$(QEMU) -drive file=$<,format=raw,index=0,media=disk

fmt:
	clang-format -i --style=WebKit $(SRCS) $(wildcard include/*.h)

clean:
	$(RM) -r $(BUILD_DIR) $(BOOT_IMAGE)
