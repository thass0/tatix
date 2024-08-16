        [bits 32]
        ;; Writes a null-terminated string whose address is found in bx straight to the VGA buffer.
print_string32:
        pusha
        
        VGA_BUF equ 0xb8000
        WB_COLOR equ 0xf

        mov edx, VGA_BUF

print_string32_loop:
        cmp byte [ebx], 0
        je print_string32_return

        mov al, [ebx]
        mov ah, WB_COLOR
        mov [edx], ax

        add ebx, 1              ; Next character
        add edx, 2              ; Next VGA buffer cell
        jmp print_string32_loop

print_string32_return:
        popa
        ret
