	section .init_proc
	global _init_proc

_init_proc:
	; mov rax, 0xcafe
	; mov rbx, 0xcafe
	mov dx, 0x3f8
	mov ax, 'X'
	out dx, ax
    int 0x80
	jmp _init_proc
