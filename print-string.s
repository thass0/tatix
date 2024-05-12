        ;; Uses the BIOS to print a null-terminated string whose address is found in bx.
print_string:
        pusha

        mov ah, 0x0e

print_string_loop:
        cmp byte [bx], 0
        je print_string_return

        mov al, [bx]
        int 0x10

        inc bx
        jmp print_string_loop

print_string_return:
        popa
        ret


        ;; Same as print_string but appends a \r\n at the end
print_line:
        call print_string
        push ax

        mov ah, 0x0e
        mov al, 0xd             ; Carriage return
        int 0x10
        mov al, 0xa             ; Line feed
        int 0x10
        
        pop ax
        ret
