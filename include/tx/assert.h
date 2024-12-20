#ifndef __TX_ASSERT_H__
#define __TX_ASSERT_H__

#include <tx/asm.h>
#include <tx/base.h>
#include <tx/print.h>
#include <tx/stringdef.h>

#define crash(msg) __crash(msg, __FILE__, __LINE__)

#define __crash(msg, file, line)                                \
    do {                                                        \
        print_str(STR(file ":" STRINGIFY(line) ": " msg "\n")); \
        hlt();                                                  \
    } while (0)

#define assert(x) __assert((x), #x, __FILE__, __LINE__)

#define __assert(x, x_str, file, line)                           \
    do {                                                         \
        if (!(x))                                                \
            __crash("Assertion '" x_str "' failed", file, line); \
    } while (0)

#endif // __TX_ASSERT_H__
