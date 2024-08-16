// Interrupt service routines

#include <tx/isr.h>
#include <tx/base.h>
#include <tx/vga.h>
#include <tx/pic.h>
#include <tx/arena.h>
#include <tx/fmt.h>

void fmt_cpu_state(struct cpu_state *cpu_state, struct fmt_buf *buf)
{
    fmt_str(STR("rax:        "), buf); fmt_hex(cpu_state->rax, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("rbx:    "), buf); fmt_hex(cpu_state->rbx, buf); fmt_char('\n', buf);
    fmt_str(STR("rcx:        "), buf); fmt_hex(cpu_state->rcx, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("rdx:    "), buf); fmt_hex(cpu_state->rdx, buf); fmt_char('\n', buf);
    fmt_str(STR("rsi:        "), buf); fmt_hex(cpu_state->rsi, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("rdi:    "), buf); fmt_hex(cpu_state->rdi, buf); fmt_char('\n', buf);
    fmt_str(STR("rbp:        "), buf); fmt_hex(cpu_state->rbp, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("r8:     "), buf); fmt_hex(cpu_state->r8, buf); fmt_char('\n', buf);
    fmt_str(STR("r9:         "), buf); fmt_hex(cpu_state->r9, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("r10:    "), buf); fmt_hex(cpu_state->r10, buf); fmt_char('\n', buf);
    fmt_str(STR("r11:        "), buf); fmt_hex(cpu_state->r11, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("r12:    "), buf); fmt_hex(cpu_state->r12, buf); fmt_char('\n', buf);
    fmt_str(STR("r13:        "), buf); fmt_hex(cpu_state->r13, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("r14:    "), buf); fmt_hex(cpu_state->r14, buf); fmt_char('\n', buf);
    fmt_str(STR("r15:        "), buf); fmt_hex(cpu_state->r15, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("vector: "), buf); fmt_hex(cpu_state->vector, buf); fmt_char('\n', buf);
    fmt_str(STR("error code: "), buf); fmt_hex(cpu_state->error_code, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("rip:    "), buf); fmt_hex(cpu_state->rip, buf); fmt_char('\n', buf);
    fmt_str(STR("cs:         "), buf); fmt_hex(cpu_state->cs, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("rflags: "), buf); fmt_hex(cpu_state->rflags, buf); fmt_char('\n', buf);
    fmt_str(STR("rsp:        "), buf); fmt_hex(cpu_state->rsp, buf); fmt_str(STR("  "), buf);
    fmt_str(STR("ss:     "), buf); fmt_hex(cpu_state->ss, buf);
}

void handle_interrupt(struct cpu_state *cpu_state)
{
    enum { arn_size = 1024 };
    u8 arn_buf[arn_size];
    struct arena arn = NEW_ARENA(arn_buf, arn_size);

    if (cpu_state->vector < RESERVED_VECTORS_END) {
        struct fmt_buf buf = NEW_FMT_BUF(&arn, 800);

        fmt_cpu_state(cpu_state, &buf);
        vga_println_with_color(STR("Error: caught unimplemented system interrupt\nSystem state:"),
                               VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_println_with_color(FMT_2_STR(buf),
                               VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        HLT();
    } else if (cpu_state->vector < IRQ_VECTORS_END) {
        vga_println(STR("Caught an IRQ"));
    }
}
