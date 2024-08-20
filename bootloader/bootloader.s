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
	;; Load stage 2 from disk                                                   ;;
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

	;; We can use BIOS routines here to load the kernel
	mov si, disk_address_packet
        mov ah, 0x42
        mov dl, 0x80 ; Driver number 0x80 for C drive
        int 0x13

        jc end16

        jmp load_gdt

; error_reading_disk:
        ;; After the interrupt, [dap_sectors_num] is the number of sectors actually read.
        ;; If fewer sectors have been read than specified in the disk address packet, the
        ;; carry bit is set to indicate an error. For flexibility reasons, we don't care
        ;; about the exact number of sectors read for now and just read as much as possible.
        ;; So, we ignore the error "too few sectors read" and just continue.
        ;; cmp word [dap_sectors_num], READ_SECTORS_NUM
        ;; jle ignore_disk_read_error
        ;; jmp end16

        ;; Data for disk read
        align 4
disk_address_packet:
        db 0x10                 ; Size of packet
        db 0                    ; Reserved, always 0
dap_sectors_num:
        dw (BOOT_SECTOR_COUNT - 1) ; Read all remaining sectors in the bootloader
        dd (BOOT_LOAD_ADDR + BOOT_SECTOR_SIZE) ; Put them in memory right after this sector
        dq 1 ; Disk block to start at (skip boot sector in block 0)

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
        jmp BOOT_GDT_CODE_DESC:start_prot_mode

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
        mov ax, BOOT_GDT_DATA_DESC
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

        jmp BOOT_GDT_CODE_DESC:start_long_mode

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
        mov ax, BOOT_GDT_DATA_DESC
        mov ds, ax
        mov ss, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        extern load_kernel
        call load_kernel

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

;; Definition of a Global Descriptor Table (GDT) that implements
;; Intel's basic "flat model" of memory segmentation.
;; See volume 3, chapter 3 of Intel's IA-32 architecture manual,
;; Protected-mode Memory Management
;;
;; Memory segmentation employs logical addresses. The logical address
;; consists of two parts: a segment selector and an offset. In this case,
;; the segment selector is an offset into the GDT. The segment descriptor
;; in the GDT that's selected contains the base address of the segment in
;; linear address space. Adding the offset part of the logical address to
;; the base address of the segment gives the linear address of the byte
;; that the logical address refers to. Without paging, linear address
;; space is physical address space (that applies here).
;;
;; The basic "flat model" comprises a code segment and a data segment. Both
;; of these segments are mapped to the entire linear address space (their
;; base addresses and limits are identical). The base is 0 and the limit is
;; 4GB (the limit fields in 20 bits and the granularity is set to 4KB,
;; 2^20 * 4KB  = 4GB). Using this simplest of all models is fine since we
;; just want to get to 64-bit mode.

;; Base address of GDT should be aligned on an eight-byte boundary
align 8

gdt32_start:
;; 8-byte null descriptor (index 0).
;; Used to catch translations with a null selector.
dd 0x0
dd 0x0

gdt32_code_segment:
;; 8-byte code segment descriptor (index 1).
;; First 16 bits of segment limit
dw 0xffff
;; First 24 bits of segment base address
dw 0x0000
db 0x00
;; 0-3: segment type that specifies an execute/read code segment
;;   4: descriptor type flag indicating that this is a code/data segment
;; 5-6: Descriptor privilege level 0 (most privileged)
;;   7: Segment present flag set indicating that the segment is present
db 10011010b
;; 0-3: last 4 bits of segment limit
;;   4: unused (available for use by system software)
;;   5: 64-bit code segment flag indicates that the segment doesn't contain 64-bit code
;;   6: default operation size of 32 bits
;;   7: granularity of 4 kilobyte units
db 11001111b
;; Last 8 bits of segment base address
db 0x00

gdt32_data_segment:
;; Only differences are explained ...
dw 0xffff
dw 0x0000
db 0x00
;; 0-3: segment type that specifies a read/write data segment
db 10010010b
;; 6: must be set for 32-bit operand size for stack
db 11001111b
db 0x00

gdt32_end:

;; Value for GDTR register that describes the above GDT
gdt32_pseudo_descriptor:
;; A limit value of 0 results in one valid byte. So, the limit value of our
;; GDT is its length in bytes minus 1.
dw gdt32_end - gdt32_start - 1
;; Start address of the GDT
dd gdt32_start


;; See gdt32.s for comments. This GDT is identical to the one for 32-bit
;; protected/long mode except that 64-bit-specific features are turned on.
;;
;; The segment limit of code and data segments is not checked in 64-bit mode
;; (see IA-32 manual, volume 3, section 5.3.1). This means that we are not
;; constrained by the 4G limit that we had in protected mode.

align 16
gdt64_start:
;; 8-byte null descriptor (index 0).
;; Used to catch translations with a null selector.
dd 0x0
dd 0x0

gdt64_code_segment:
;; 8-byte code segment descriptor (index 1).
;; First 16 bits of segment limit
dw 0xffff
;; First 24 bits of segment base address
dw 0x0000
db 0x00
;; 0-3: segment type that specifies an execute/read code segment
;;   4: descriptor type flag indicating that this is a code/data segment
;; 5-6: Descriptor privilege level 0 (most privileged)
;;   7: Segment present flag set indicating that the segment is present
db 10011010b
;; 0-3: last 4 bits of segment limit
;;   4: unused (available for use by system software)
;;   5: 64-bit code segment flag indicates that this segment contains 64-bit code
;;   6: must be zero if L bit (bit 5) is set
;;   7: granularity of 4 kilobyte units
db 10101111b
;; Last 8 bits of segment base address
db 0x00

gdt64_data_segment:
;; Only differences are explained ...
dw 0xffff
dw 0x0000
db 0x00
;; 0-3: segment type that specifies a read/write data segment
db 10010010b
;; 6: must be set for 32-bit operand size for stack
db 11101111b
db 0x00

gdt64_end:

gdt64_pseudo_descriptor:
dw gdt64_end - gdt64_start - 1
dd gdt64_start
