        ;; Stub ISRs used to retrieve the interrupt vector and return control
        ;; to a single common interrupt handler.

        section .text

        extern handle_interrupt

        ;; Save context and call the actual interrupt handler
isr_stub_common:
        push r15
        push r14
        push r13
        push r12
        push r11
        push r10
        push r9
        push r8
        push rbp
        push rdi
        push rsi
        push rdx
        push rcx
        push rbx
        push rax

        mov rdi, rsp            ; Pass `handle_interrupt` the CPU state as a struct by pointer
        call handle_interrupt

        pop rax
        pop rbx
        pop rcx
        pop rdx
        pop rsi
        pop rdi
        pop rbp
        pop r8
        pop r9
        pop r10
        pop r11
        pop r12
        pop r13
        pop r14
        pop r15

        ;; Pop the interrupt vector and the error code. Without a privilege level switch,
        ;; the IRET instruction only pops the RIP, CS and RFLAGS registers, so we need to
        ;; delete the interrupt vector and the error code manually.
        add rsp, 16

        iretq

        ;; Macro to define an ISR stub for exceptions with errors codes on the stack
%macro isr_error_code_stub 1
isr_stub_%+%1:
        push %1
        jmp isr_stub_common
%endmacro

        ;; Macro to define an ISR stub for exceptions or interrupts without error codes
%macro isr_stub 1
isr_stub_%+%1:
        ;; There's no error code on the stack from the CPU in this ISR but we want to present the interrupt
        ;; handler with identical-looking stacks regardless of whether there's an error code. So we push a
        ;; dummy value before the vector.
        push 0
        push %1
        jmp isr_stub_common
%endmacro

        ;; Declare all available ISR stubs
        
        ;; Reserved interrupt vectors
        ;; Table 6-1 in Volume 3 of the IA-32 manual shows which exceptions have error codes.
        isr_stub            0
        isr_stub            1
        isr_stub            2
        isr_stub            3
        isr_stub            4
        isr_stub            5
        isr_stub            6
        isr_stub            7
        isr_error_code_stub 8
        isr_stub            9
        isr_error_code_stub 10
        isr_error_code_stub 11
        isr_error_code_stub 12
        isr_error_code_stub 13
        isr_error_code_stub 14
        isr_stub            15
        isr_stub            16
        isr_error_code_stub 17
        isr_stub            18
        isr_stub            19
        isr_stub            20
        isr_error_code_stub 21

        ;; IRQ interrupt vectors
        isr_stub 32
        isr_stub 33
        isr_stub 34
        isr_stub 35
        isr_stub 36
        isr_stub 37
        isr_stub 38
        isr_stub 39
        isr_stub 40
        isr_stub 41
        isr_stub 42
        isr_stub 43
        isr_stub 44
        isr_stub 45
        isr_stub 46
        isr_stub 47


        section .data
        
        ;; These tables are useful because they're easy to iterate over. That way, registering all the
        ;; ISR stubs in the IDT takes much less code that writing out every stub label on its own would.
        global isr_stub_reserved_table
isr_stub_reserved_table:
%assign i 0
%rep 22
        dq isr_stub_%+i
%assign i (i + 1)
%endrep

        global isr_stub_irq_table
isr_stub_irq_table:
%assign i 32
%rep 16
        dq isr_stub_%+i
%assign i (i + 1)
%endrep
