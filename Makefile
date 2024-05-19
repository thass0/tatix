.PHONY = clean boot count-boot-sec-zeros

BOOT_IMAGE := boot-sector.bin
QEMU := qemu-system-x86_64 -no-reboot

$(BOOT_IMAGE): boot-sector.s print-string.s gdt32.s gdt64.s paging.s
	nasm $< -f bin -o $@

boot: $(BOOT_IMAGE)
	$(QEMU) -drive file=$<,format=raw,index=0,media=disk

clean:
	$(RM) boot-sector.bin
