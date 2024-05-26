#ifndef _VGA_H_
#define _VGA_H_

#include <base.h>
#include <string.h>

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_PURPLE = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_GRAY = 7,
    VGA_COLOR_DARK_GRAY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_PURPLE = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15,
};

typedef u8 vga_color_attr;

#define VGA_COLOR(FG_COLOR, BG_COLOR) \
    ((u8)(BG_COLOR) << 4 | (u8)(FG_COLOR))

__attribute__ ((no_caller_saved_registers))
void vga_print(struct str);
__attribute__ ((no_caller_saved_registers))
void vga_print_with_color(struct str, vga_color_attr);
__attribute__ ((no_caller_saved_registers))
void vga_println(struct str);
__attribute__ ((no_caller_saved_registers))
void vga_println_with_color(struct str, vga_color_attr);

__attribute__ ((no_caller_saved_registers))
void vga_clear_screen(void);

#endif // _VGA_H_
