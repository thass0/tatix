        [org 0x7c00]                    ; BIOS loads this boot sector to address 0x7c00

        call a20_check
        cmp ax, 0
        je error_a20_line_not_set

        mov bx, msg_entered_real_mode
        call print_string_16

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Load GDT and switch to protected mode ;;
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        cli                     ; Can't have interrupts during the switch
        lgdt [gdt_pseudo_descriptor]

        ;; Setting cr0.PE (bit 0) enables protected mode
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        ;; The far jump into the code segment from the new GDT flushes
        ;; the CPU pipeline removing any 16-bit decoded instructions
        ;; and updates the cs register with the new code segment.
        jmp CODE_SEG:start_prot_mode

error_a20_line_not_set:
        mov bx, msg_a20_line_not_set
        call print_string_16
end:
        hlt
        jmp end

%include "gdt.s"
%include "print-string.s"
%include "a20.s"

        [bits 32]
start_prot_mode:
        ;; Old segments are now meaningless
        mov ax, DATA_SEG
        mov ds, ax
        mov ss, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        mov ebp, 0x90000
        mov esp, ebp

        mov bx, msg_switched_to_prot_mode
        call print_string_32

        jmp $

msg_entered_real_mode:          db "Entered 16-bit real mode", 13, 10, 0
msg_switched_to_prot_mode:      db "Successfully switched to 32-bit protected mode", 0
msg_a20_line_not_set:           db "Error: a20 line isn't enabled", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xaa55        
