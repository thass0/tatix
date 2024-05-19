        ;; Manage A20 line legacy weirdness of x86 :(
        ;; Source of code in this file: https://wiki.osdev.org/A20_Line

        [bits 16]
        ;; Returns 1 in ax if the a20 line is enabled and 0 otherwise.
a20_check:
        pushf
        push ds
        push es
        push di
        push si
        
        cli
        
        xor ax, ax ; ax = 0
        mov es, ax
        
        not ax ; ax = 0xFFFF
        mov ds, ax
        
        mov di, 0x0500
        mov si, 0x0510
        
        mov al, byte [es:di]
        push ax
        
        mov al, byte [ds:si]
        push ax
        
        mov byte [es:di], 0x00
        mov byte [ds:si], 0xFF
        
        cmp byte [es:di], 0xFF
        
        pop ax
        mov byte [ds:si], al
        
        pop ax
        mov byte [es:di], al
        
        mov ax, 0
        je a20_check_exit
        
        mov ax, 1
        
a20_check_exit:
        pop si
        pop di
        pop es
        pop ds
        popf
        
        ret

