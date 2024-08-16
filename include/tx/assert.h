#ifndef __TX_ASSERT_H__
#define __TX_ASSERT_H__

#include <tx/base.h>
#include <tx/string.h>
#include <tx/vga.h>

#define __STRINGIFY(x) #x
#define assert(x) __assert((x), #x, __FILE__, __LINE__)
#define __assert(x, x_str, file, line)                                  \
    do {                                                                \
        if (!(x)) {                                                     \
            vga_println_with_color(STR(__STRINGIFY(file) ":" __STRINGIFY(line) ": Assertion '" x_str "' failed"), \
                                   VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK)); \
            HLT();                                                      \
        }                                                               \
    } while (0);

#endif // __TX_ASSERT_H__
