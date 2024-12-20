	section .init_proc
	global _init_proc

_init_proc:
    mov dx, 0x3f8
    mov ax, 'X'
    out dx, ax
    ;; Let's write something
    mov rax, 1 ; Syscall number write
    mov rdi, 0 ; fd is not used
    mov rsi, msg ; rsi holds the pointer to the string
    mov rdx, msg_len ; rdx holds the length of the string in bytes
    int 0x80

    ;; Now we read some input
    mov rax, 0 ; Syscall number read
    ;; rdi still contains the unused fd numebr
    mov rsi, buf ; rsi holds the buffer to read to
    mov rdx, buf_len ; rdx holds the number of bytes to read
    int 0x80

loop:
    pause
    jmp loop

msg:
    db "Please enter your name:", 10
msg_len equ $ - msg

buf_len equ 256
buf:
    times buf_len db 0
