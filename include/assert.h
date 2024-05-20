#ifndef _ASSERT_H_
#define _ASSERT_H_

#include <base.h>
#include <string.h>
#include <vga.h>

#define __STRINGIFY(x) #x
#define assert(x) __assert((x), #x, __FILE__, __LINE__)
#define __assert(x, x_str, file, line)                                                                            \
    do {                                                                                                          \
        if (!(x)) {                                                                                               \
            vga_println_with_color(STR(__STRINGIFY(file) ":" __STRINGIFY(line) ": Assertion '" x_str "' failed"), \
                VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));                                                 \
            while (true) {                                                                                        \
                __asm__("hlt");                                                                                   \
            }                                                                                                     \
        }                                                                                                         \
    } while (0);

#endif // _ASSERT_H_
