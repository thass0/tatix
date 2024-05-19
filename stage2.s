        [org BOOT_LOAD_ADDR + SECTOR_SIZE]
        [bits 16]
        
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

        ;; Kernel is in the third sector
        KERNEL_ADDR equ BOOT_LOAD_ADDR + 2 * SECTOR_SIZE
        call KERNEL_ADDR

end64:
        hlt
        jmp end64

%include "gdt32.s"
%include "gdt64.s"
%include "paging.s"
%include "print-string.s"       ; TODO: Split the printing functions
        
msg_switched_to_comp_mode:      db "Entered 64-bit compatibility mode", 0

times SECTOR_SIZE - ($ - $$) db 0
