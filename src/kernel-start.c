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

void kernel_start(void)
{
    struct arena arn = {
        .beg = global_kernel_arena_buffer,
        .end = global_kernel_arena_buffer + global_kernel_arena_buffer_size,
    };

    vga_clear_screen();
    vga_println(STR("Hello, world!"), &arn);
    vga_println_with_color(STR("What will you do here?"), VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK), &arn);
}
