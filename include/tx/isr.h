#ifndef __TX_ISR_H__
#define __TX_ISR_H__

#include <tx/base.h>

struct cpu_state {
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 rsi;
    u64 rdi;
    u64 rbp;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;

    u64 vector;
    u64 error_code;

    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

void handle_interrupt(struct cpu_state *);

// Ranges for different types of interrupt vectors. Given as intervals: [beg; end)
#define RESERVED_VECTORS_BEG 0
#define RESERVED_VECTORS_END 32
#define NUM_RESERVED_VECTORS (RESERVED_VECTORS_END - RESERVED_VECTORS_BEG)
#define NUM_USED_RESERVED_VECTORS 22 // Based on the manual, only the first 22 reserved vectors are used
#define IRQ_VECTORS_BEG RESERVED_VECTORS_END
#define IRQ_VECTORS_END 48
#define NUM_IRQ_VECTORS (IRQ_VECTORS_END - IRQ_VECTORS_BEG)

#endif // __TX_ISR_H__
