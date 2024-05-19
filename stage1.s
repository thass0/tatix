        READ_SECTORS_NUM equ 127
        
        ;; BIOS loads this boot sector to address 0x7c00
        [bits 16]
        [org BOOT_LOAD_ADDR]

        call a20_check
        cmp ax, 0
        je error_a20_line_not_set

        mov bx, msg_entered_real_mode
        call print_string16

        ;; Load the second stage of the bootloader using BIOS

        mov si, disk_address_packet
        mov ah, 0x42
        mov dl, 0x80
        int 0x13
        jc error_reading_disk

ignore_disk_read_error:
        SND_STAGE_ADDR equ (BOOT_LOAD_ADDR + SECTOR_SIZE)
        jmp 0:SND_STAGE_ADDR


error_reading_disk:
        ;; After the interrupt, [dap_sectors_num] is the number of sectors actually read.
        ;; If fewer sectors have been read than specified in the disk address packet, the
        ;; carry bit is set to indicate an error. For flexibility reasons, we don't care
        ;; about the exact number of sectors read for now and just read as much as possible.
        ;; So, we ignore the error "too few sectors read" and just continue.
        cmp word [dap_sectors_num], READ_SECTORS_NUM
        jle ignore_disk_read_error
        mov bx, msg_error_reading_disk
        call print_string16
        jmp end16
error_num_sectors_read:
        mov bx, msg_error_num_sectors_read
        call print_string16
        jmp end16
error_a20_line_not_set:
        mov bx, msg_a20_line_not_set
        call print_string16
end16:
        hlt
        jmp end16

%include "a20.s"
%include "print-string16.s"

        align 4
disk_address_packet:
        db 0x10                 ; Size of packet
        db 0                    ; Reserved, always 0
dap_sectors_num:
        dw READ_SECTORS_NUM               ; Number of sectors to read
        dd (BOOT_LOAD_ADDR + SECTOR_SIZE) ; Read destination address
        dq 1                    ; Disk block to start at (skip boot sector in block 0)

msg_entered_real_mode:  db "Entered 16-bit real mode", 13, 10, 0
msg_a20_line_not_set:   db "Error: a20 line is clear", 13, 10, 0
msg_error_reading_disk: db "Error: failed to read disk with int 0x13/ah=0x40", 13, 10, 0
msg_error_num_sectors_read:     db "Error: number of sectors requested doesn't match number of sectors read", 13, 10, 0

        ;; End of the boot sector:
times (SECTOR_SIZE - 2) - ($ - $$) db 0
dw 0xaa55
