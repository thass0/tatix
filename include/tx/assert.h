#ifndef __TX_ASSERT_H__
#define __TX_ASSERT_H__

#include <tx/base.h>
#include <tx/asm.h>
#include <tx/string.h>
#include <tx/vga.h>

#define crash(msg) __crash(msg, __FILE__, __LINE__)

#define __crash(msg, file, line)                                                 \
    do {                                                                         \
        vga_println_with_color(STR(file ":" STRINGIFY(line) ": " msg),           \
                               VGA_COLOR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK)); \
        hlt();                                                                   \
    } while (0)

#define assert(x) __assert((x), #x, __FILE__, __LINE__)

#define __assert(x, x_str, file, line)                           \
    do {                                                         \
        if (!(x))                                                \
            __crash("Assertion '" x_str "' failed", file, line); \
    } while (0)

#endif // __TX_ASSERT_H__
