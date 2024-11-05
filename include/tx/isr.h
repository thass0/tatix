#ifndef __TX_ISR_H__
#define __TX_ISR_H__

#include <tx/base.h>

struct trap_frame {
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
} __packed;

static_assert(sizeof(struct trap_frame) == 176);

void isr_stub_0(void);
void isr_stub_1(void);
void isr_stub_2(void);
void isr_stub_3(void);
void isr_stub_4(void);
void isr_stub_5(void);
void isr_stub_6(void);
void isr_stub_7(void);
void isr_stub_8(void);
void isr_stub_9(void);
void isr_stub_10(void);
void isr_stub_11(void);
void isr_stub_12(void);
void isr_stub_13(void);
void isr_stub_14(void);
void isr_stub_15(void);
void isr_stub_16(void);
void isr_stub_17(void);
void isr_stub_18(void);
void isr_stub_19(void);
void isr_stub_20(void);
void isr_stub_21(void);

void isr_stub_32(void);
void isr_stub_33(void);
void isr_stub_34(void);
void isr_stub_35(void);
void isr_stub_36(void);
void isr_stub_37(void);
void isr_stub_38(void);
void isr_stub_39(void);
void isr_stub_40(void);
void isr_stub_41(void);
void isr_stub_42(void);
void isr_stub_43(void);
void isr_stub_44(void);
void isr_stub_45(void);
void isr_stub_46(void);
void isr_stub_47(void);

void isr_stub_128(void);

__naked void isr_return(void);
void handle_interrupt(struct trap_frame *);

// Ranges for different types of interrupt vectors. Given as intervals: [beg; end)
#define RESERVED_VECTORS_BEG 0
#define RESERVED_VECTORS_END 32
#define NUM_RESERVED_VECTORS (RESERVED_VECTORS_END - RESERVED_VECTORS_BEG)
#define NUM_USED_RESERVED_VECTORS 22 // Based on the manual, only the first 22 reserved vectors are used
#define IRQ_VECTORS_BEG RESERVED_VECTORS_END
#define IRQ_VECTORS_END 48
#define NUM_IRQ_VECTORS (IRQ_VECTORS_END - IRQ_VECTORS_BEG)

#define IRQ_SYSCALL 0x80

#endif // __TX_ISR_H__
