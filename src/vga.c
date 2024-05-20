#include <vga.h>
#include <mem.h>

struct vga_char {
    char ch;
    vga_color_attr attr;
};

#define VGA_SCREEN_WIDTH 80
#define VGA_SCREEN_HEIGHT 25
#define VGA_PAGE_SIZE (VGA_SCREEN_WIDTH * VGA_SCREEN_HEIGHT * sizeof(struct vga_char))

volatile static struct vga_char* vga_buffer = (volatile struct vga_char*)0xb8000;
static i64 col = 0;
static i64 row = 0;

static inline i32 vga_next_row(struct arena scratch)
{
    if (row < VGA_SCREEN_HEIGHT) {
        row++;
    } else {
        // Scroll up one line.
        return memmove_volatile(vga_buffer, vga_buffer + VGA_SCREEN_WIDTH, VGA_PAGE_SIZE, scratch);    
    }
    return 0;
}

static i32 vga_print_internal(struct str str, vga_color_attr color_attr, bool add_linefeed, struct arena scratch)
{
    if (STR_IS_NULL(str))
        return -1;

    i64 offset = 0;
    for (i32 i = 0; i < str.len; i++) {
        offset = (row * VGA_SCREEN_WIDTH) + col;
        vga_buffer[offset].ch = str.dat[i];
        vga_buffer[offset].attr = color_attr;

        col++;
        if (col >= VGA_SCREEN_WIDTH) {
            col -= VGA_SCREEN_WIDTH;
            vga_next_row(scratch);
        }
    }

    if (add_linefeed) {
        col = 0;
        vga_next_row(scratch);
    }

    return 0;
}

i32 vga_print(struct str str, struct arena scratch)
{
    return vga_print_internal(str, VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK), false, scratch);
}

i32 vga_print_with_color(struct str str, vga_color_attr color, struct arena scratch)
{
    return vga_print_internal(str, color, false, scratch);
}

i32 vga_println(struct str str, struct arena scratch)
{
    return vga_print_internal(str, VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK), true, scratch);
}

i32 vga_println_with_color(struct str str, vga_color_attr color, struct arena scratch)
{
    return vga_print_internal(str, color, true, scratch);
}

void vga_clear_screen(void)
{
    row = 0;
    col = 0;
    for (i32 i = 0; i < VGA_SCREEN_WIDTH * VGA_SCREEN_HEIGHT; i++) {
        vga_buffer[i].ch = 0;
        vga_buffer[i].attr = 0;
    }
}
