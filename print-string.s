        [bits 16]
        ;; Uses the BIOS to print a null-terminated string whose address is found in bx.
print_string_16:
        pusha

        mov ah, 0x0e

print_string_16_loop:
        cmp byte [bx], 0
        je print_string_16_return

        mov al, [bx]
        int 0x10

        inc bx
        jmp print_string_16_loop

print_string_16_return:
        popa
        ret


        [bits 32]
        ;; Writes a string null-terminated whose address is found in bx straight to the VGA buffer.
print_string_32:
        pusha

        VGA_BUF equ 0xb8000
        WB_COLOR equ 0xf

        mov edx, VGA_BUF

print_string_32_loop:
        cmp byte [ebx], 0
        je print_string_32_return

        mov al, [ebx]
        mov ah, WB_COLOR
        mov [edx], ax

        add ebx, 1              ; Next character
        add edx, 2              ; Next VGA buffer cell
        jmp print_string_32_loop

print_string_32_return:
        popa
        ret
