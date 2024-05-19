.PHONY = clean boot

# Configure how many sectors the bootloader should read from the boot disk
READ_SECTORS_NUM ?= 64

BUILD_DIR := build
BOOTLOADER_DIR := bootloader

QEMU := qemu-system-x86_64 -no-reboot
NASM := nasm -dSECTOR_SIZE=512 -dBOOT_LOAD_ADDR=0x7c00 -I$(BOOTLOADER_DIR)

BOOT_IMAGE := image.bin

$(BOOT_IMAGE): $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin
	cat $^ > $@

$(BUILD_DIR)/stage1.bin: $(BOOTLOADER_DIR)/stage1.s | $(BUILD_DIR)
	$(NASM) -dREAD_SECTORS_NUM=$(READ_SECTORS_NUM) $< -f bin -o $@

$(BUILD_DIR)/stage2.bin: $(BOOTLOADER_DIR)/stage2.s | $(BUILD_DIR)
	$(NASM) $< -f bin -o $@

$(BUILD_DIR)/kernel.bin: kernel.c | $(BUILD_DIR)
	gcc -ffreestanding -m64 -fno-builtin -nostdinc -c $^ -o $(BUILD_DIR)/kernel.o
	ld -Ttext 0x8000 -o $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.o
	objcopy -O binary $(BUILD_DIR)/kernel.elf $@

$(BUILD_DIR):
	mkdir $@

boot: $(BOOT_IMAGE)
	$(QEMU) -drive file=$<,format=raw,index=0,media=disk

clean:
	$(RM) -r $(BUILD_DIR) $(BOOT_IMAGE)
