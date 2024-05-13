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
        ;; base addresses and limits are identical). Using this simplest of all
        ;; models is fine since we just want to get to 64-bit mode.

        ;; Base address of GDT should be aligned on an eight-byte boundary
        align 8
        
gdt_start:
        ;; 8-byte null descriptor (index 0).
        ;; Used to catch translations with a null selector.
        dd 0x0
        dd 0x0

gdt_code_segment:
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

gdt_data_segment:
        ;; Only differences are explained ...
        dw 0xffff
        dw 0x0000
        db 0x00
        ;; 0-3: segment type that specifies a read/write data segment
        db 10010010b
        db 11001111b
        dw 0x00

gdt_end:

        ;; Value for GDTR register that describes the above GDT
gdt_pseudo_descriptor:
        ;; A limit value of 0 results in one valid byte. So, the limit value of our
        ;; GDT is its length in bytes minus 1.
        dw gdt_end - gdt_start - 1
        ;; Start address of the GDT
        dd gdt_start


CODE_SEG equ gdt_code_segment - gdt_start
DATA_SEG equ gdt_data_segment - gdt_start
