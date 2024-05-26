#ifndef _ISR_H_
#define _ISR_H_

#include <base.h>

struct interrupt_frame {
    u64 rip;
    u64 cs;
    u64 sflags;
    u64 rsp;
    u64 ss;
};

__attribute__ ((interrupt))
void handle_invalid_interrupt(struct interrupt_frame *frame);

__attribute__ ((interrupt))
void handle_keyboard_interrupt(struct interrupt_frame *frame);

__attribute__ ((interrupt))
void handle_example_interrupt(struct interrupt_frame *frame);

#endif // _ISR_H_
