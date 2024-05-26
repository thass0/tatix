        ;; See gdt32.s for comments. This GDT is identical to the one for 32-bit
        ;; protected/long mode except that 64-bit-specific features are turned on.

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
        db 10101111b
        dw 0x00

gdt64_end:

gdt64_pseudo_descriptor:
        dw gdt64_end - gdt64_start - 1
        dd gdt64_start


CODE_SEG64 equ gdt64_code_segment - gdt64_start
DATA_SEG64 equ gdt64_data_segment - gdt64_start
