.PHONY = clean boot

BOOT_IMAGE := boot-sector.bin
QEMU := qemu-system-x86_64 -no-reboot

$(BOOT_IMAGE): boot-sector.s print-string.s gdt.s
	nasm $< -f bin -o $@

boot: $(BOOT_IMAGE)
	$(QEMU) -drive file=$<,format=raw,index=0,media=disk

clean:
	$(RM) boot-sector.bin
