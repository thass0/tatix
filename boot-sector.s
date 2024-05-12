        [org 0x7c00]                    ; BIOS loads this boot sector to address 0x7c00

        mov bx, hello_world
        call print_line

        mov bx, 0x1fb6
        call print_hex

        jmp $

print_hex:
        mov di, fmt_hex_buf
        call fmt_hex
        mov byte [fmt_hex_buf + 6], 0
        mov bx, fmt_hex_buf
        call print_string
        ret

%include "fmt-hex.s"
%include "print-string.s"

hello_world:    db "Hello, world!", 0
fmt_hex_buf:    times 7 db 0

times 510 - ($ - $$) db 0
dw 0xaa55
