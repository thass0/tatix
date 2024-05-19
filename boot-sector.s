        ;; BIOS loads this boot sector to address 0x7c00
        [org 0x7c00]
        [bits 16]

        call a20_check
        cmp ax, 0
        je error_a20_line_not_set

        mov bx, msg_entered_real_mode
        call print_string_16

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Load GDT and switch to protected mode ;;
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        cli                     ; Can't have interrupts during the switch
        lgdt [gdt32_pseudo_descriptor]

        ;; Setting cr0.PE (bit 0) enables protected mode
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        ;; The far jump into the code segment from the new GDT flushes
        ;; the CPU pipeline removing any 16-bit decoded instructions
        ;; and updates the cs register with the new code segment.
        jmp CODE_SEG32:start_prot_mode

error_a20_line_not_set:
        mov bx, msg_a20_line_not_set
        call print_string_16
end16:
        hlt
        jmp end16


        [bits 32]
start_prot_mode:
        ;; Old segments are now meaningless
        mov ax, DATA_SEG32
        mov ds, ax
        mov ss, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        ;; Re-locate the stack now that more memory is addressable
        mov ebp, 0x90000
        mov esp, ebp

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Build 4 level page table and switch to long mode ;;
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        mov ebx, 0x1000
        call page64_build_4_level_page_table
        mov cr3, ebx            ; MMU finds the PML4 table in cr3

        ;; Enable Physical Address Extension (PAE)
        mov eax, cr4
        or eax, 1 << 5
        mov cr4, eax

        ;; The EFER (Extended Feature Enable Register) MSR (Model-Specific Register) contains fields
        ;; related to IA-32e mode operation. Bit 8 if this MSR is the LME (long mode enable) flag
        ;; that enables IA-32e operation.
        mov ecx, 0xc0000080
        rdmsr
        or eax, 1 << 8
        wrmsr

        ;; Enable paging (PG flag in cr0, bit 31)
        mov eax, cr0
        or eax, 1 << 31
        mov cr0, eax

        mov ebx, msg_switched_to_comp_mode
        call print_string_32

        ;; New GDT has the 64-bit segment flag set
        lgdt [gdt64_pseudo_descriptor]

        jmp CODE_SEG64:start_long_mode

end32:
        hlt
        jmp end32


        [bits 64]
start_long_mode:
        ;; Old segments are even more meaningless now (because long mode doesn't care)
        mov ax, DATA_SEG64
        mov ds, ax
        mov ss, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        ;; Clear the screen
        mov edi, 0xb8000
        xor rax, rax
        mov ecx, 500
        rep stosq

end64:
        hlt
        jmp end64


%include "print-string.s"
%include "gdt32.s"
%include "gdt64.s"
%include "a20.s"
%include "paging.s"

msg_entered_real_mode:          db "Entered real mode", 13, 10, 0
msg_switched_to_comp_mode:      db "Entered 64-bit compatibility mode", 0
msg_a20_line_not_set:           db "Error: a20 line not set", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xaa55        
