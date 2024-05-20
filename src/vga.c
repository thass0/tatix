#include <vga.h>
#include <mem.h>

struct vga_char {
    char ch;
    vga_color_attr attr;
};

#define VGA_SCREEN_WIDTH 80
#define VGA_SCREEN_HEIGHT 25

volatile static struct vga_char* vga_buffer = (volatile struct vga_char*)0xb8000;
static i64 col;
static i64 row;

static i32 vga_print_internal(struct str str, vga_color_attr color_attr, bool add_linefeed, struct arena *arn)
{
    if (STR_IS_NULL(str) || arn == NULL)
        return -1;

    i64 offset = (row * VGA_SCREEN_WIDTH) + col;
    for (i32 i = 0; i < str.len; i++) {
        vga_buffer[i + offset].ch = str.dat[i];
        vga_buffer[i + offset].attr = color_attr;
    }

    col += str.len;
    if (col >= VGA_SCREEN_WIDTH) {
        col -= VGA_SCREEN_WIDTH;
        row += 1;
    }

    if (add_linefeed) {
        col = 0;
        row += 1;
    }

    // This is scrolling
    if (row > VGA_SCREEN_HEIGHT) {
        row -= 1;
        // Move all memory in the buffer one line back
        memmove_volatile(vga_buffer - VGA_SCREEN_WIDTH, vga_buffer,
                         VGA_SCREEN_HEIGHT * VGA_SCREEN_WIDTH * 2, arn);
    }

    return 0;
}

int vga_print(struct str str, struct arena *arn)
{
    return vga_print_internal(str, VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK), false, arn);
}

int vga_print_with_color(struct str str, vga_color_attr color, struct arena *arn)
{
    return vga_print_internal(str, color, false, arn);
}

int vga_println(struct str str, struct arena *arn)
{
    return vga_print_internal(str, VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK), true, arn);
}

int vga_println_with_color(struct str str, vga_color_attr color, struct arena *arn)
{
    return vga_print_internal(str, color, true, arn);
}

void vga_clear_screen(void)
{
    row = 0;
    col = 0;
    for (int i = 0; i < VGA_SCREEN_WIDTH * VGA_SCREEN_HEIGHT; i++) {
        vga_buffer[i].ch = 0;
        vga_buffer[i].attr = 0;
    }
}
