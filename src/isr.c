// Interrupt service routines

#include <tx/arena.h>
#include <tx/base.h>
#include <tx/fmt.h>
#include <tx/isr.h>
#include <tx/pic.h>

void fmt_cpu_state(struct cpu_state *cpu_state, struct str_buf *buf)
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
        print_str(STR("Caught an IRQ\n"));
    }
}
