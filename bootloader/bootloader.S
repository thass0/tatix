	section .bootloader
	global start_real_mode

	[bits 16]

start_real_mode:
	cli
	;; BIOS doesn't make guarantees about the segment registers so
	;; we should zero all of them. ds and es are used in the A20
	;; line check. They'll both be reset to zero after the check.
	mov ax, 0
	mov es, ax ; es must be 0 for the A20 line check below
	mov ds, ax
	mov ss, ax
	mov fs, ax
	mov gs, ax

	;; Put the stack right before this code
	mov bp, start_real_mode
	mov sp, bp

	mov si, real_mode_msg
	mov cx, REAL_MODE_MSG_LEN
	call print16

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	;; Check A20 Line                                                           ;;
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	;; Check if the A20 line is set. This is x86 legacy weirdness.
	;; Code from: https://wiki.osdev.org/A20_Line

	not ax ; ax = 0xffff
        mov ds, ax

	mov di, 0x0500
	mov si, 0x0510

	mov al, byte [es:di]
	push ax

	mov al, byte [ds:si]
	push ax

	mov byte [es:di], 0x00
	mov byte [ds:si], 0xff

	cmp byte [es:di], 0xff

	pop ax
	mov byte [ds:si], al

	pop ax
	mov byte [es:di], al

A20_LINE_SET equ 1
A20_LINE_UNSET equ 0

	;; If the previous comparison returned "equal", the A20 line is _not_ set.
	;; We want to store this result and restore the registers that we dirtied
	;; before we branch based on this result.
	mov bx, A20_LINE_SET
	mov dx, A20_LINE_UNSET
	cmove bx, dx

	;; Zero these segment registers again
	mov ax, 0
	mov es, ax
	mov ds, ax

	cmp bx, A20_LINE_UNSET
	je end16

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	;; Load kernel from disk                                                    ;;
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	;; We can use BIOS routines here to load the kernel
	mov si, disk_address_packet
        mov ah, 0x42
        mov dl, 0x80 ; Driver number 0x80 for C drive
        int 0x13

        jc error_reading_disk

ignore_disk_read_error:
        jmp load_gdt

error_reading_disk:
        ;; After the interrupt, [dap_sectors_num] is the number of sectors actually read.
        ;; If fewer sectors have been read than specified in the disk address packet, the
        ;; carry bit is set to indicate an error. For flexibility reasons, we don't care
        ;; about the exact number of sectors read for now and just read as much as possible.
        ;; So, we ignore the error "too few sectors read" and just continue.
        cmp word [dap_sectors_num], READ_SECTORS_NUM
        jle ignore_disk_read_error
        jmp end16

        ;; Data for disk read
        align 4
disk_address_packet:
        db 0x10                 ; Size of packet
        db 0                    ; Reserved, always 0
dap_sectors_num:
        dw READ_SECTORS_NUM               ; Number of sectors to read
        dd (BOOT_LOAD_ADDR + SECTOR_SIZE) ; Read destination address
        dq 1                    ; Disk block to start at (skip boot sector in block 0)

SECTOR_SIZE equ 512
BOOT_LOAD_ADDR equ 0x7c00
READ_SECTORS_NUM equ 64

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	;; Load 32-bit GDT and switch to 32-bit protected mode                      ;;
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

load_gdt:
	lgdt [gdt32_pseudo_descriptor]

	;; Setting cr0.PE (bit 0) enables protected mode
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        ;; The far jump into the code segment from the new GDT flushes
        ;; the CPU pipeline removing any 16-bit decoded instructions
        ;; and updates the cs register with the new code segment.
        jmp CODE_SEG32:start_prot_mode

print16:
	mov dx, COM1_PORT
.loop:
	lodsb ; al = *si++
	out dx, al
	loop .loop
	ret

end16:
	hlt
	jmp end16

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	;; Set up paging, load 64-bit GDT and switch to 64-bit long mode            ;;
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        [bits 32]

start_prot_mode:
	mov esi, prot_mode_msg
	mov ecx, PROT_MODE_MSG_LEN
	call print32

        ;; Old segments are now meaningless
        mov ax, DATA_SEG32
        mov ds, ax
        mov ss, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        ;; Build 4 level page table and switch to long mode
        mov ebx, 0x1000
        call build_page_table
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

        ;; New GDT has the 64-bit segment flag set
        lgdt [gdt64_pseudo_descriptor]

        jmp CODE_SEG64:start_long_mode

        ;; Builds a 4 level page table starting at the address that's passed in ebx.
build_page_table:
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
set_page_table_entry:
        mov dword [edi], ebx
        add ebx, PAGE64_PAGE_SIZE
        add edi, 8
        loop set_page_table_entry

        popa
        ret

print32:
	mov dx, COM1_PORT
.loop:
	lodsb ; al = *si++
	out dx, al
	loop .loop
	ret
end32:
        hlt
        jmp end32

        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Enter the kernel in 64-bit mode                                          ;;
        ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        [bits 64]

start_long_mode:
	mov esi, long_mode_msg
	mov ecx, LONG_MODE_MSG_LEN
	call print32

        ;; Old segments are even more meaningless now (because long mode doesn't care)
        mov ax, DATA_SEG64
        mov ds, ax
        mov ss, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        extern kernel_init
        call kernel_init

end64:
        hlt
        jmp end64

COM1_PORT equ 0x3f8
real_mode_msg: db "Start real mode", 0xa
REAL_MODE_MSG_LEN equ $ - real_mode_msg
prot_mode_msg: db "Start prot mode", 0xa
PROT_MODE_MSG_LEN equ $ - prot_mode_msg
long_mode_msg: db "Booted long mode", 0xa
LONG_MODE_MSG_LEN equ $ - long_mode_msg

%include "gdt32.S"
%include "gdt64.S"
