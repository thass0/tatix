.PHONY = clean boot count-boot-sec-zeros

BOOT_IMAGE := boot-sector.bin
QEMU := qemu-system-x86_64 -no-reboot

$(BOOT_IMAGE): boot-sector.s print-string.s gdt.s page32.s
	nasm $< -f bin -o $@

boot: $(BOOT_IMAGE)
	$(QEMU) -drive file=$<,format=raw,index=0,media=disk

count-boot-sec-zeros: $(BOOT_IMAGE)
	@echo "Number of zero bytes in the boot sector:"
	@tr -cd '\0' < $(BOOT_IMAGE) | wc -c

clean:
	$(RM) boot-sector.bin
