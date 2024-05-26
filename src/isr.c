// Interrupt service routines

#include <isr.h>
#include <base.h>
#include <vga.h>
#include <pic.h>

void handle_interrupt(struct cpu_state *cpu_state)
{
    if (cpu_state->rax == 0xc0ffeecafe) {
        vga_println(STR("I set up the CPU state struct correctly"));
        while (true)
            __asm__ volatile ("hlt");
    } else {
        vga_println(STR("I DID NOT set up the CPU state correctly"));
    }

    if (cpu_state->vector < 32) {
        vga_println(STR("Caught a system exception"));
    } else if (cpu_state->vector < 48) {
        vga_println(STR("Caught an IRQ"));
    }
}
