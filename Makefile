.PHONY = clean boot fmt

BUILD_DIR := build
BOOTLOADER_DIR := bootloader
SRC_DIR := src

C_SRCS := $(wildcard $(SRC_DIR)/*.c)
C_OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS))
C_DEPS := $(C_OBJS:%.o=%.d)

ASM_SRCS := $(BOOTLOADER_DIR)/stage1.s $(BOOTLOADER_DIR)/stage2.s
ASM_OBJS := $(patsubst $(BOOTLOADER_DIR)/%.s, $(BUILD_DIR)/%.o, $(ASM_SRCS))

OBJS := $(C_OBJS) $(ASM_OBJS)

BOOT_IMAGE := $(BUILD_DIR)/image.bin

$(BOOT_IMAGE): $(BUILD_DIR)/kernel.o
	objcopy -O binary $< $@

$(BUILD_DIR)/kernel.o: $(OBJS) | $(BUILD_DIR)
	ld -T linker.ld -o $@ $^

$(BUILD_DIR)/%.o: $(BOOTLOADER_DIR)/%.s | $(BUILD_DIR)
	nasm -I$(BOOTLOADER_DIR) -f elf64 $< -o $@

# To re-compile if headers change:
-include $(C_DEPS)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	gcc -MMD -Iinclude/ -std=c99 -ffreestanding -m64 -fno-builtin -nostdinc -Wall -Wextra -pedantic -c $< -o $@

$(BUILD_DIR):
	mkdir $@

boot: $(BOOT_IMAGE)
	qemu-system-x86_64 -no-reboot -drive file=$<,format=raw,index=0,media=disk

fmt:
	clang-format -i --style=WebKit $(SRCS) $(wildcard include/*.h)

clean:
	$(RM) -r $(BUILD_DIR)
