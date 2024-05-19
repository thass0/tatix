        [bits 32]
        ;; Builds a 4 level page table starting at the address that's passed in ebx.
page64_build_4_level_page_table:
        pusha

        ;; Initialize 512 64-bit page directory entries. All flags in the page directory
        ;; entries are set to 0 except for the enable read/write flag (bit 1).

        PAGE64_PAGE_SIZE equ 0x1000
        PAGE64_TAB_SIZE equ 0x1000
        PAGE64_TAB_ENT_NUM equ 512

        ;; Initialize all four tables to 0. If the present flag is cleared, all other bits in any
        ;; entry are ignored. So by filling all entries with zeros, they are all "not present".
        ;; Each repetition zeros four bytes at once. That's why a number of repetitions equal to
        ;; the size of a single page table is enough to zero all four tables.
        mov ecx, PAGE64_TAB_SIZE ; ecx stores the number of repetitions
        mov edi, ebx             ; edi stores the base address
        xor eax, eax             ; eax stores the value
        rep stosd

        ;; Link first entry in PML4 table to the PDP table
        mov edi, ebx
        lea eax, [edi + (PAGE64_TAB_SIZE | 11b)] ; Set read/write and present flags
        mov dword [edi], eax

        ;; Link first entry in PDP table to the PD table
        add edi, PAGE64_TAB_SIZE
        add eax, PAGE64_TAB_SIZE
        mov dword [edi], eax

        ;; Link the first entry in the PD table to the page table
        add edi, PAGE64_TAB_SIZE
        add eax, PAGE64_TAB_SIZE
        mov dword [edi], eax

        ;; Identity map the first 2 MB of memory in the single page table
        add edi, PAGE64_TAB_SIZE
        mov ebx, 11b
        mov ecx, PAGE64_TAB_ENT_NUM
page64_set_entry:
        mov dword [edi], ebx
        add ebx, PAGE64_PAGE_SIZE
        add edi, 8
        loop page64_set_entry

        popa
        ret
