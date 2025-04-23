#ifndef __TX_ASM_H__
#define __TX_ASM_H__

#include <tx/base.h>

#define hlt()                        \
    do {                             \
        while (true)                 \
            __asm__ volatile("hlt"); \
        __builtin_unreachable();     \
    } while (0)

static inline void cpuid(u32 leaf /* eax */, u32 subleaf /* ecx */, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(subleaf));
    *eax = a;
    *ebx = b;
    *ecx = c;
    *edx = d;
}

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline void outw(u16 port, u16 val)
{
    __asm__ volatile("outw %w0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline void outl(u16 port, u32 val)
{
    __asm__ volatile("outl %0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ volatile("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline u32 inl(u16 port)
{
    u32 ret;
    __asm__ volatile("inl %w1, %0" : "=a"(ret) : "Nd"(port) : "memory");
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

static inline void write_cr3(u64 cr3)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static inline u64 read_cr3(void)
{
    u64 cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

#endif // __TXT_ASM_H__
