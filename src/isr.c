// Interrupt service routines

#include <tx/arena.h>
#include <tx/assert.h>
#include <tx/base.h>
#include <tx/fmt.h>
#include <tx/isr.h>
#include <tx/pic.h>

///////////////////////////////////////////////////////////////////////////////
// Interrupt handling                                                        //
///////////////////////////////////////////////////////////////////////////////

struct interrupt_handler {
    u8 is_used;
    interrupt_handler_func_t handler;
    void *private_data;
};

static struct interrupt_handler handler_table[IRQ_VECTORS_END];

struct result isr_register_handler(u64 vector, interrupt_handler_func_t handler, void *private_data)
{
    assert(handler);

    if (vector >= IRQ_VECTORS_END)
        return result_error(EINVAL);

    if (handler_table[vector].is_used)
        return result_error(EINVAL);

    handler_table[vector].is_used = true;
    handler_table[vector].handler = handler;
    handler_table[vector].private_data = private_data;

    return result_ok();
}

static bool have_interrupt_handler(u64 vector)
{
    if (vector >= IRQ_VECTORS_END)
        return false;
    return handler_table[vector].is_used;
}

static void handle_interrupt(struct trap_frame *cpu_state)
{
    if (cpu_state->vector < IRQ_VECTORS_END) {
        struct interrupt_handler *handler = &handler_table[cpu_state->vector];
        if (handler->is_used) {
            handler->handler(cpu_state, handler->private_data);
        }
    }

    pic_send_eoi(cpu_state->vector);
}

///////////////////////////////////////////////////////////////////////////////
// Catch-all ISR                                                             //
///////////////////////////////////////////////////////////////////////////////

__naked __unused void isr_stub_common(void)
{
    __asm__ volatile("push %r15");
    __asm__ volatile("push %r14");
    __asm__ volatile("push %r13");
    __asm__ volatile("push %r12");
    __asm__ volatile("push %r11");
    __asm__ volatile("push %r10");
    __asm__ volatile("push %r9");
    __asm__ volatile("push %r8");
    __asm__ volatile("push %rbp");
    __asm__ volatile("push %rdi");
    __asm__ volatile("push %rsi");
    __asm__ volatile("push %rdx");
    __asm__ volatile("push %rcx");
    __asm__ volatile("push %rbx");
    __asm__ volatile("push %rax");

    // Pass `interrupt_catch_all` the CPU state as a struct by pointer
    __asm__ volatile("mov %rsp, %rdi");
    __asm__ volatile("call interrupt_catch_all");

    __asm__ volatile("jmp isr_return");
}

__naked __unused void isr_return(void)
{
    __asm__ volatile("pop %rax");
    __asm__ volatile("pop %rbx");
    __asm__ volatile("pop %rcx");
    __asm__ volatile("pop %rdx");
    __asm__ volatile("pop %rsi");
    __asm__ volatile("pop %rdi");
    __asm__ volatile("pop %rbp");
    __asm__ volatile("pop %r8");
    __asm__ volatile("pop %r9");
    __asm__ volatile("pop %r10");
    __asm__ volatile("pop %r11");
    __asm__ volatile("pop %r12");
    __asm__ volatile("pop %r13");
    __asm__ volatile("pop %r14");
    __asm__ volatile("pop %r15");

    // Pop the interrupt vector and the error code. Without a privilege level switch,
    // the IRET instruction only pops the RIP, CS and RFLAGS registers, so we need to
    // delete the interrupt vector and the error code manually.
    __asm__ volatile("add $16, %rsp");
    __asm__ volatile("iretq");
}

#define ISR_STUB_WITH_ERROR_CODE(vector)              \
    __naked void isr_stub_##vector(void)              \
    {                                                 \
        __asm__ volatile("push $" STRINGIFY(vector)); \
        __asm__ volatile("jmp isr_stub_common");      \
    }

#define ISR_STUB(vector)                                       \
    __naked void isr_stub_##vector(void)                       \
    {                                                          \
        __asm__ volatile("push $0; push $" STRINGIFY(vector)); \
        __asm__ volatile("jmp isr_stub_common");               \
    }

ISR_STUB(0)
ISR_STUB(1)
ISR_STUB(2)
ISR_STUB(3)
ISR_STUB(4)
ISR_STUB(5)
ISR_STUB(6)
ISR_STUB(7)
ISR_STUB_WITH_ERROR_CODE(8)
ISR_STUB(9)
ISR_STUB_WITH_ERROR_CODE(10)
ISR_STUB_WITH_ERROR_CODE(11)
ISR_STUB_WITH_ERROR_CODE(12)
ISR_STUB_WITH_ERROR_CODE(13)
ISR_STUB_WITH_ERROR_CODE(14)
ISR_STUB(15)
ISR_STUB(16)
ISR_STUB_WITH_ERROR_CODE(17)
ISR_STUB(18)
ISR_STUB(19)
ISR_STUB(20)
ISR_STUB_WITH_ERROR_CODE(21)

// IRQ interrupt vectors
ISR_STUB(32)
ISR_STUB(33)
ISR_STUB(34)
ISR_STUB(35)
ISR_STUB(36)
ISR_STUB(37)
ISR_STUB(38)
ISR_STUB(39)
ISR_STUB(40)
ISR_STUB(41)
ISR_STUB(42)
ISR_STUB(43)
ISR_STUB(44)
ISR_STUB(45)
ISR_STUB(46)
ISR_STUB(47)

static void fmt_cpu_state(struct trap_frame *cpu_state, struct str_buf *buf)
{
    fmt(buf,
        STR("rax: 0x%lx\nrbx: 0x%lx\nrcx: 0x%lx\nrdx: 0x%lx\nrsi: 0x%lx\nrdi: 0x%lx\nrbp: 0x%lx\n"
            "r8: 0x%lx\nr9: 0x%lx\nr10: 0x%lx\nr11: 0x%lx\nr12: 0x%lx\nr13: 0x%lx\nr14: 0x%lx\nr15: 0x%lx\n"
            "vector: 0x%lx\nerror code: 0x%lx\nrip: 0x%lx\ncs: 0x%lx\nrflags: 0x%lx\nrsp: 0x%lx\nss: 0x%lx\n"),
        cpu_state->rax, cpu_state->rbx, cpu_state->rcx, cpu_state->rdx, cpu_state->rsi, cpu_state->rdi, cpu_state->rbp,
        cpu_state->r8, cpu_state->r9, cpu_state->r10, cpu_state->r11, cpu_state->r12, cpu_state->r13, cpu_state->r14,
        cpu_state->r15, cpu_state->vector, cpu_state->error_code, cpu_state->rip, cpu_state->cs, cpu_state->rflags,
        cpu_state->rsp, cpu_state->ss);
}

void __unused interrupt_catch_all(struct trap_frame *cpu_state)
{
    char underlying[1024];
    struct str_buf buf = str_buf_new(underlying, 0, countof(underlying));

    if (have_interrupt_handler(cpu_state->vector)) {
        handle_interrupt(cpu_state);
        return;
    }

    print_str(STR("*** Interrupt handler failed\n"));

    fmt_cpu_state(cpu_state, &buf);

    if (cpu_state->vector < RESERVED_VECTORS_END)
        print_str(STR("Error: System interrupt:\n"));
    else
        print_str(STR("Error: Unexpected interrupt:\n"));

    print_str(str_from_buf(buf));

    crash("Failed to handle interrupt\n");
}
