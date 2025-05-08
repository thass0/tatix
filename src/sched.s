    global sched_do_context_switch
    global sched_do_final_context_switch

    section .text

; void sched_to_context_switch(byte **old_sp, byte *new_sp)
; Parameter(s):
;   old_sp (rdi) - pointer to where to store current stack pointer
;   new_sp (rsi) - new stack pointer to switch to
sched_do_context_switch:
    ; Save callee-saved registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save current stack pointer to the location pointed to by first argument (rdi)
    mov [rdi], rsp

    ; Load new stack pointer from second argument (rsi)
    mov rsp, rsi

    ; Restore callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

; void sched_to_final_context_switch(u64 *new_sp)
; Parameter(s):
;   new_sp (rdi) - new stach pointer to switch to
sched_do_final_context_switch:
    mov rsp, rdi

    ; Restore callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret
