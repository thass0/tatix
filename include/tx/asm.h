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

static inline u64 rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | (u64)lo;
}

static inline bool rdrand_u64(u64 *result)
{
    if (!result)
        return false;

    u8 success;

    for (int i = 0; i < 10; i++) {
        __asm__ volatile("rdrand %0\n\t"
                         "setc %1"
                         : "=r"(*result), "=qm"(success)
                         :
                         : "cc");

        if (success)
            return true;
    }

    return false;
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

static inline u64 mmio_read64(u64 addr)
{
    u64 val;
    __asm__ volatile("mfence; movq (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}
static inline void mmio_write64(u64 addr, u64 val)
{
    __asm__ volatile("movq %1, (%0); mfence" : : "r"(addr), "r"(val) : "memory");
}

static inline u32 mmio_read32(u64 addr)
{
    u32 val;
    __asm__ volatile("mfence; movl (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}
static inline void mmio_write32(u64 addr, u32 val)
{
    __asm__ volatile("movl %1, (%0); mfence" : : "r"(addr), "r"(val) : "memory");
}

static inline u16 mmio_read16(u64 addr)
{
    u16 val;
    __asm__ volatile("mfence; movw (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}
static inline void mmio_write16(u64 addr, u16 val)
{
    __asm__ volatile("movw %1, (%0); mfence" : : "r"(addr), "r"(val) : "memory");
}

static inline u8 mmio_read8(u64 addr)
{
    u8 val;
    __asm__ volatile("mfence; movb (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}
static inline void mmio_write8(u64 addr, u8 val)
{
    __asm__ volatile("movb %1, (%0); mfence" : : "r"(addr), "r"(val) : "memory");
}

#endif // __TXT_ASM_H__
