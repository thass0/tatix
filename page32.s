        [bits 16]
        ;; Set up a page directory at the address passed in bx. After the page directory
        ;; comes a single page table that identity maps first 4 MB of physical addresses.
page32_setup_dir:
        pusha

        ;; Initialize 1024 32-bit page directory entries. All flags in the page directory
        ;; entries are set to 0 except for the enable read/write flag (bit 1).

        PAGE_TAB_ENT_NUM equ 1024
        PAGE_TAB_ENT_SIZE equ 4
        PAGE_TAB_BYTE_SIZE equ PAGE_TAB_ENT_SIZE * PAGE_TAB_ENT_NUM

        xor cx, cx
setup_next_dir_ent:
        mov dword [ebx + ecx * PAGE_TAB_ENT_SIZE], 0x00000002
        inc cx
        cmp cx, PAGE_TAB_ENT_NUM
        jl setup_next_dir_ent

        ;; Now initialize 1024 32-bit page table entries. All flags in the page table
        ;; entries are set to 0 except for the present flag (bit 0) and the read/write
        ;; flag (bit 1).
        ;;
        ;; edx stores the page table entry of the current iteration. Such an entry consists
        ;; of a 4096 byte aligned address the more significant bits and flag bits in the
        ;; lower 12 bits (since those bits are zero for all 4096 byte aligned addresses).

        mov edx, 11b
        xor cx, cx
setup_next_tab_ent:
        mov dword [ebx + ecx * PAGE_TAB_ENT_SIZE + PAGE_TAB_BYTE_SIZE], edx
        add edx, 4096
        inc cx
        cmp cx, PAGE_TAB_ENT_NUM
        jl setup_next_tab_ent

        ;; Set the first entry in the page directory so that it points
        ;; to the page table that was just created.
        lea eax, [bx + PAGE_TAB_BYTE_SIZE]
        or eax, 11b             ; Set the read/write and present flags (bits 0 and 1)
        mov dword [bx], eax

        popa
        ret
