#include <tx/vga.h>

struct vga_char {
    char ch;
    vga_color_attr attr;
};

#define VGA_SCREEN_WIDTH 80
#define VGA_SCREEN_HEIGHT 25
#define VGA_PAGE_SIZE (VGA_SCREEN_WIDTH * VGA_SCREEN_HEIGHT * sizeof(struct vga_char))

static volatile struct vga_char *vga_buffer = (volatile struct vga_char *)0xb8000;
static i64 col = 0;
static i64 row = 0;

__attribute__((no_caller_saved_registers)) static inline void vga_next_row(void)
{
    if (row < VGA_SCREEN_HEIGHT) {
        row++;
    } else {
        // Scroll up by one line
        for (i64 row_ix = 0; row_ix < VGA_SCREEN_HEIGHT; row_ix++) {
            for (i64 col_ix = 0; col_ix < VGA_SCREEN_WIDTH; col_ix++) {
                vga_buffer[((row_ix)*VGA_SCREEN_WIDTH) + col_ix] =
                    vga_buffer[((row_ix + 1) * VGA_SCREEN_WIDTH) + col_ix];
            }
        }
    }
}

__attribute__((no_caller_saved_registers)) static void vga_print_internal(struct str str, vga_color_attr color_attr,
                                                                          bool add_linefeed)
{
    if (STR_IS_NULL(str))
        return;

    i64 offset = 0;
    for (i32 i = 0; i < str.len; i++) {
        if (str.dat[i] == '\n') {
            col = 0;
            vga_next_row();
            continue;
        }

        offset = (row * VGA_SCREEN_WIDTH) + col;
        vga_buffer[offset].ch = str.dat[i];
        vga_buffer[offset].attr = color_attr;

        col++;
        if (col >= VGA_SCREEN_WIDTH) {
            col -= VGA_SCREEN_WIDTH;
            vga_next_row();
        }
    }

    if (add_linefeed) {
        col = 0;
        vga_next_row();
    }
}

void vga_print(struct str str)
{
    vga_print_internal(str, VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK), false);
}

void vga_print_with_color(struct str str, vga_color_attr color)
{
    vga_print_internal(str, color, false);
}

void vga_println(struct str str)
{
    vga_print_internal(str, VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK), true);
}

void vga_println_with_color(struct str str, vga_color_attr color)
{
    vga_print_internal(str, color, true);
}

void vga_clear_screen(void)
{
    row = 0, col = 0;
    for (i32 i = 0; i < VGA_SCREEN_WIDTH * VGA_SCREEN_HEIGHT; i++) {
        vga_buffer[i].ch = 0;
        vga_buffer[i].attr = 0;
    }
}
