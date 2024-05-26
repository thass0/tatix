// Interrupt service routines

#include <isr.h>
#include <base.h>
#include <vga.h>
#define ISR
#include <pic.h>
#undef ISR

void handle_invalid_interrupt(struct interrupt_frame *frame)
{
    vga_println_with_color(STR("Error: Invalid interrupt occurred"), VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    while (true)
        __asm__ ("hlt");
}

void handle_keyboard_interrupt(struct interrupt_frame *frame)
{
    vga_println(STR("Wow, I just pressed a key!"));
}

void handle_example_interrupt(struct interrupt_frame *frame)
{
    vga_println(STR("This example works indeed!"));
}
