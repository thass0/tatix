#include <string.h>
#include <vga.h>
#include <arena.h>

#include <assert.h>

// TODO:
// 1. String formatter (depends on (1))
// 2. Printing to serial console (create a unified kprint that prints to console and vga)

#define global_kernel_arena_buffer_size 10000
u8 global_kernel_arena_buffer[global_kernel_arena_buffer_size];

void kernel_start(void)
{
    struct arena arn = {
        .beg = global_kernel_arena_buffer,
        .end = global_kernel_arena_buffer + global_kernel_arena_buffer_size,
    };

    vga_clear_screen();

    vga_println(STR("Hello, world!"));
    vga_println_with_color(STR("What will you do here?"), VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));

    for (i32 i = 0; i < 17; i++)
        vga_println(STR("Don't know what to print"));

    vga_println(STR("This is a really long line. In fact, it's so long that it's difficult "
                    "to think of a name for it! Nonetheless, we need to go ahead and make this "
                    "line even longer so that we can find out if wrapping works!"));
    
    vga_println_with_color(STR("Another cyan message 1"), VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_println_with_color(STR("Another cyan message 2."), VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_println_with_color(STR("Another cyan message 3."), VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    assert(NULL != NULL);
    vga_println(STR("Hello world"));
}
