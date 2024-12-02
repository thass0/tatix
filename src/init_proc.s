	section .init_proc
	global _init_proc

_init_proc:
	; mov rax, 0xcafe
	; mov rbx, 0xcafe
	mov dx, 0x3f8
	mov ax, 'X'
	out dx, ax
	mov rax, 1 ; Syscall write
	mov rdi, 0 ; fd is not used
	mov rsi, msg ; rsi holds the pointer to the string
	mov rdx, len ; rdx holds the length of the string in bytes
    int 0x80

loop:
    pause
    jmp loop

msg:
    db "Hello, I am a user-space program", 10
len equ $ - msg
