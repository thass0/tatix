// The second stage bootloader expects to find the kernel entry point
// right at the start of the next sector after the sector of the second
// stage bootloader itself. So, we must place the entry point right here
// at the start of the file and then jump to the true entry point.
void true_start(void);
void _start(void) { true_start(); }

#include <string.h>
#include <vga.h>

void true_start(void)
{
    vga_clear_screen();
    vga_println(STR("Hello, world!"));
    vga_println_with_color(STR("What will you do here?"), VGA_COLOR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    for (i32 i = 0; i < 24; i++)
        vga_println(STR("Blah"));
}
