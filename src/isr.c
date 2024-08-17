// Interrupt service routines

#include <tx/arena.h>
#include <tx/base.h>
#include <tx/fmt.h>
#include <tx/isr.h>
#include <tx/pic.h>

void fmt_cpu_state(struct cpu_state *cpu_state, struct str_buf *buf)
{
    fmt(buf,
        "rax: %lx\nrbx: %lx\nrcx: %lx\nrdx: %lx\nrsi: %lx\nrdi: %lx\nrbp: %lx\n"
        "r8: %lx\nr9: %lx\nr10: %lx\nr11: %lx\nr12: %lx\nr13: %lx\nr14: %lx\nr15: %lx\n"
        "vector: %lx\nerror code: %lx\nrip: %lx\ncs: %lx\nrflags: %lx\nrsp: %lx\nss: %lx\n",
        cpu_state->rax, cpu_state->rbx, cpu_state->rcx, cpu_state->rdx, cpu_state->rsi, cpu_state->rdi, cpu_state->rbp,
        cpu_state->r8, cpu_state->r9, cpu_state->r10, cpu_state->r11, cpu_state->r12, cpu_state->r13, cpu_state->r14,
        cpu_state->r15, cpu_state->vector, cpu_state->error_code, cpu_state->rip, cpu_state->cs, cpu_state->rflags,
        cpu_state->rsp, cpu_state->ss);
}

void handle_interrupt(struct cpu_state *cpu_state)
{
    char underlying[1024];
    struct str_buf buf = str_buf_new(underlying, 0, countof(underlying));

    if (cpu_state->vector < RESERVED_VECTORS_END) {
        fmt_cpu_state(cpu_state, &buf);
        print_str(STR("Error: caught unimplemented system interrupt\nSystem state:\n"));
        print_str(str_from_buf(buf));
        hlt();
    } else if (cpu_state->vector < IRQ_VECTORS_END) {
        print_str(STR("Caught an IRQ"));
    }
}
