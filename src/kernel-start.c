// The second stage bootloader expects to find the kernel entry point
// right at the start of the next sector after the sector of the second
// stage bootloader itself. So, we must place the entry point right here
// at the start of the file and then jump to the true entry point.
void true_start(void);
void _start(void) { true_start(); }

#include <string.h>
#include <vga.h>
#include <arena.h>

// TODO:
// 1. Arena allocator
// 2. String formatter (depends on (1))
// 3. Make vga printing more solid and independent and use it in the abort handler
// 4. Printing to serial console (create a unified kprint that prints to console and vga)

#define global_kernel_arena_buffer_size 1000
u8 global_kernel_arena_buffer[global_kernel_arena_buffer_size];

void true_start(void)
{
    struct arena arn = {
        .beg = global_kernel_arena_buffer,
        .end = global_kernel_arena_buffer + global_kernel_arena_buffer_size,
    };

    vga_clear_screen();
    vga_println(STR("Hello, world!"), &arn);
    vga_println_with_color(STR("What will you do here?"), VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK), &arn);
    for (i32 i = 0; i < 24; i++)
        vga_println(STR("Blah"), &arn);
}
