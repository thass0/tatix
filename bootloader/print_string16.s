        [bits 16]
        ;; Uses the BIOS to print a null-terminated string whose address is found in bx.
print_string16:
        pusha

        mov ah, 0x0e

print_string16_loop:
        cmp byte [bx], 0
        je print_string16_return

        mov al, [bx]
        int 0x10

        inc bx
        jmp print_string16_loop

print_string16_return:
        popa
        ret
