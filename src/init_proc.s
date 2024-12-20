	section .init_proc
	global _init_proc

_init_proc:
    ;; Let's write something
    mov rax, 1 ; Syscall number write
    mov rdi, 0 ; fd is not used
    mov rsi, msg1 ; rsi holds the pointer to the string
    mov rdx, msg1_len ; rdx holds the length of the string in bytes
    int 0x80

    ;; Now we read some input
    mov rax, 0 ; Syscall number read
    ;; rdi still contains the unused fd numebr
    mov rsi, buf ; rsi holds the buffer to read to
    mov rdx, buf_len ; rdx holds the number of bytes to read
    int 0x80
    mov r8, rax ; Save the exit code, it's the number of characters read

    ;; Let's write the output so the users sees it
    mov rax, 1 ; Syscall number write
    mov rsi, msg2
    mov rdx, msg2_len
    int 0x80
    mov rax, 1
    mov rsi, buf
    mov rdx, r8
    int 0x80


loop:
    pause
    jmp loop

msg1:
    db "Please enter your name: "
msg1_len equ $ - msg1

msg2:
    db "Your name is: "
msg2_len equ $ - msg2

buf_len equ 256
buf:
    times buf_len db 0
