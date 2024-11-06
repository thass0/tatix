#ifndef __TX_ASM_H__
#define __TX_ASM_H__

#include <tx/base.h>

#define hlt()                        \
    do {                             \
        while (true)                 \
            __asm__ volatile("hlt"); \
        __builtin_unreachable();     \
    } while (0)

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ volatile("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline void disable_interrupts(void)
{
    __asm__ volatile("cli");
}

static inline void enable_interrupts(void)
{
    __asm__ volatile("sti");
}

static inline void insl(u16 port, void *addr, u32 cnt)
{
    __asm__ volatile("cld; rep insl" : "=D"(addr), "=c"(cnt) : "d"(port), "0"(addr), "1"(cnt) : "memory", "cc");
}

static inline void stosb(void *addr, u32 data, u32 cnt)
{
    __asm__ volatile("cld; rep stosb" : "=D"(addr), "=c"(cnt) : "0"(addr), "1"(cnt), "a"(data) : "memory", "cc");
}

static inline void lgdt(volatile void *addr)
{
    __asm__ volatile("lgdt (%0)" : : "r"(addr));
}

static inline void ltr(u16 selector)
{
    __asm__ volatile("ltr %0" : : "r"(selector));
}

static inline void load_cr3(u64 cr3)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

#endif // __TXT_ASM_H__
