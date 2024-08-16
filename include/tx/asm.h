#ifndef __TX_ASM_H__
#define __TX_ASM_H__

#include <tx/base.h>

#define hlt()                           \
    do {                                \
        while (true)                    \
            __asm__ volatile ("hlt");   \
    } while (0)

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile ("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ volatile ("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

#endif // __TXT_ASM_H__
