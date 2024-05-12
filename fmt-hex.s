        ;; Turn the number found in bx into a hexadecimal ASCII string representation
        ;; The string is written to the address in di. It's six characters long:
        ;; two for the '0x' prefix and four for the digits of a 16-bit number.
fmt_hex:
        pusha

        ;; Make it pretty with '0x' at the start.
        mov word [di], '0x'
        add di, 2

        ;; In ASCII, the decimal digits and hex digits don't live next to
        ;; each other. So we two branches that compute the right ASCII
        ;; character for every four bits.
        mov cx, 12
        
fmt_hex_next_char:
        mov dx, bx
        shr dx, cl
        and dx, 0xf

        ;; If the value of the nibble is greater than 9, we need
        ;; to print it with one of the characters from 'a' - 'f'.
        cmp dl, 9
        jg fmt_hex_digit

        ;; Otherwise, we can print a decimal digit.p
        add dl, '0'
        jmp fmt_hex_curr_char

fmt_hex_digit:
        add dl, 'a' - 10         ; Minimum value of dl here is 10

fmt_hex_curr_char:
        mov [di], dl
        inc di

        sub cx, 4
        cmp cx, 0
        jge fmt_hex_next_char

        popa
        ret
