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

        ;; Build 2-level page table using 1GB pages and map the first four GB of memory

        ;; We need two pages in total. Zero-initializing them is safe
        mov ecx, BOOT_PAGE_TAB_SIZE * 2
        mov edi, BOOT_PAGE_TAB_ADDR
        xor eax, eax
        rep stosb

        ;; Bits 0 and 1 enable the present flag and read/write flag, respectively.
        mov dword [BOOT_PAGE_TAB_ADDR], 11b | (BOOT_PAGE_TAB_ADDR + BOOT_PAGE_SIZE)
        ;; Bit 7 in the PDPT entries enables 1GB pages.
        mov dword [BOOT_PAGE_TAB_ADDR + BOOT_PAGE_SIZE + 0x00], 11b | (1 << 7) | (0 << 30)
        mov dword [BOOT_PAGE_TAB_ADDR + BOOT_PAGE_SIZE + 0x08], 11b | (1 << 7) | (1 << 30)
        mov dword [BOOT_PAGE_TAB_ADDR + BOOT_PAGE_SIZE + 0x10], 11b | (1 << 7) | (2 << 30)
        mov dword [BOOT_PAGE_TAB_ADDR + BOOT_PAGE_SIZE + 0x18], 11b | (1 << 7) | (3 << 30)

        mov edi, BOOT_PAGE_TAB_ADDR
        mov cr3, edi ; MMU finds the PML4 table in cr3

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

        extern load_kernel
        call load_kernel

end64:
        hlt
        jmp end64

COM1_PORT equ 0x3f8
real_mode_msg: db "Entered real mode", 0xd
REAL_MODE_MSG_LEN equ $ - real_mode_msg
prot_mode_msg: db  "Entered protected mode", 0xd
PROT_MODE_MSG_LEN equ $ - prot_mode_msg
long_mode_msg: db "Entered long mode      ", 0xa
LONG_MODE_MSG_LEN equ $ - long_mode_msg

	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	;; Global Descriptor Tables                                                 ;;
	;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

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

	;; Bits in the low config byte
GDT_CODE_TYPE		equ (1010b << 0) ; Execute/read code segment
GDT_DATA_TYPE		equ (0010b << 0) ; Read/write data segment
GDT_FLAG_S		equ (1 << 4) ; Code or data segment? (always set)
GDT_DPL			equ (00b << 5) ; Highest privilege level
GDT_FLAG_P		equ (1 << 7) ; Segment present? (always set)

	;; Bits in the high config byte
GDT32_FLAG_L		equ (0 << 5) ; Native 64-bit code? (set for 64-bit mode)
GDT64_CODE_FLAG_L	equ (1 << 5)
GDT64_DATA_FLAG_L	equ (0 << 5)
GDT32_FLAG_D		equ (1 << 6) ; Intel says always set this for 32-bit code
GDT64_FLAG_D		equ (0 << 6)
GDT_FLAG_G		equ (1 << 7) ; Count the limit in 4KB units

gdt32_start:
	;; 8-byte null descriptor (index 0) used to catch translations with a null selector.
	dd 0x0
	dd 0x0

gdt32_code_segment:
	dw 0xffff ; Segement limit 0:15
	dw 0x0000 ; Base address 0:15
	db 0x00 ; Base address 16:23
	db GDT_CODE_TYPE | GDT_FLAG_S | GDT_DPL | GDT_FLAG_P
	; Low four bits are segment limit bits 16 to 19:
	db 1111b | GDT32_FLAG_L | GDT32_FLAG_D | GDT_FLAG_G
	db 0x00 ; Base address 24:31

gdt32_data_segment:
	dw 0xffff
	dw 0x0000
	db 0x00
	db GDT_DATA_TYPE | GDT_FLAG_S | GDT_DPL | GDT_FLAG_P
	db 1111b | GDT32_FLAG_L | GDT32_FLAG_D | GDT_FLAG_G
	db 0x00

gdt32_end:

	;; Value for GDTR register that describes the above GDT
gdt32_pseudo_descriptor:
	;; A limit value of 0 results in one valid byte. So, the limit value of our
	;; GDT is its length in bytes minus 1.
	dw gdt32_end - gdt32_start - 1
	;; Start address of the GDT
	dd gdt32_start


	;; This GDT is identical to the one for 32-bit protected/long mode except that
	;; 64-bit-specific features are turned on.
	;;
	;; The segment limit of code and data segments is not checked in 64-bit mode
	;; (see IA-32 manual, volume 3, section 5.3.1). This means that we are not
	;; constrained by the 4G limit that we had in protected mode.

	align 16
gdt64_start:
	dd 0x0
	dd 0x0

gdt64_code_segment:
	dw 0xffff
	dw 0x0000
	db 0x00
	db GDT_CODE_TYPE | GDT_FLAG_S | GDT_DPL | GDT_FLAG_P
	db 1111b | GDT64_CODE_FLAG_L | GDT64_FLAG_D | GDT_FLAG_G
	db 0x00

gdt64_data_segment:
	dw 0xffff
	dw 0x0000
	db 0x00
	db GDT_DATA_TYPE | GDT_FLAG_S | GDT_DPL | GDT_FLAG_P
	db 1111b | GDT64_DATA_FLAG_L | GDT64_FLAG_D | GDT_FLAG_G
	db 0x00

gdt64_end:

gdt64_pseudo_descriptor:
	dw gdt64_end - gdt64_start - 1
	dd gdt64_start
