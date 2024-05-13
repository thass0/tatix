        [org 0x7c00]                    ; BIOS loads this boot sector to address 0x7c00

        mov bx, real_mode_msg
        call print_string_16

        mov bx, before_switch_msg
        call print_string_16

        cli                     ; Can't have interrupts during the switch
        lgdt [gdt_pseudo_descriptor]
        ;; Setting the LSB it cr0 causes the switch
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        ;; The far jump into the code segment from the new GDT flushes
        ;; the CPU pipeline removing any 16-bit decoded instructions
        ;; and updates the cs register with the new code segment.
        jmp CODE_SEG:start_prot_mode

%include "print-string.s"
%include "gdt.s"


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

        mov bx, after_switch_msg
        call print_string_32

        jmp $

real_mode_msg:          db "Booted 16-bit real mode", 13, 10, 0
before_switch_msg:      db "Preparing switch to 32-bit protected mode", 13, 10, 0
after_switch_msg:       db "Successfully switched to 32-bit protected mode", 0

times 510 - ($ - $$) db 0
dw 0xaa55
