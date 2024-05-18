        [bits 32]
page64_build_page_table:
        pusha

        ;; Initialize 1024 32-bit page directory entries. All flags in the page directory
        ;; entries are set to 0 except for the enable read/write flag (bit 1).

        ;; These apply to all four tables:
        PAGE64_ENT_NUM equ 512
        PAGE64_ENT_SIZE equ 8
        PAGE64_BYTE_SIZE equ PAGE64_ENT_SIZE * PAGE64_ENT_NUM

        PAGE64_PML4_OFF equ 0 * PAGE64_BYTE_SIZE ; Offset of page-map level 4 table
        PAGE64_PDP_OFF equ 1 * PAGE64_BYTE_SIZE ; Offset of page directory pointer table
        PAGE64_PD_OFF equ 2 * PAGE64_BYTE_SIZE ; Offset of page directory table
        PAGE64_TAB_OFF equ 3 * PAGE64_BYTE_SIZE ; Offset of page table

        ;; Initialize all auxiliary tables except for the page table itself
        ;; to all zeros. This way, all their entries have bit 0 cleared too
        ;; (bit 0 is the present flag).
        PAGE64_TOTAL_AUX_TABLE_NUM equ 3
        xor cx, cx
page64_init_next_ent:
        mov dword [ebx + ecx * PAGE64_ENT_SIZE], 0
        mov dword [ebx + ecx * PAGE64_ENT_SIZE + 4], 0
        inc cx
        cmp cx, PAGE64_ENT_NUM * PAGE64_TOTAL_AUX_TABLE_NUM
        jl page64_init_next_ent

        ;; Make the first entry in the PML4 table point to the PDP table
        lea eax, [bx + PAGE64_PDP_OFF]
        or eax, 11b             ; Set read/write and present flags
        mov dword [bx + PAGE64_PML4_OFF], eax

        ;; Make the first entry in the PDP table point to the PD table
        lea eax, [bx + PAGE64_PD_OFF]
        or eax, 11b
        mov dword [bx + PAGE64_PDP_OFF], eax

        ;; Make the first entry in the PD table point to the page table
        lea eax, [bx + PAGE64_TAB_OFF]
        or eax, 11b
        mov dword [bx + PAGE64_PD_OFF], eax

        ;; Identity map the first 4 MB of memory in the single page table
        mov edx, 11b
        xor cx, cx
page64_setup_next_tab_ent:
        mov dword [ebx + ecx * PAGE64_ENT_SIZE + PAGE64_TAB_OFF], edx
        add edx, 4096
        inc cx
        cmp cx, PAGE64_ENT_NUM
        jl page64_setup_next_tab_ent

        popa
        ret
