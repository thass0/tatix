#ifndef __TX_PROC_H__
#define __TX_PROC_H__

#include <tx/base.h>
#include <tx/bytes.h>
#include <tx/isr.h>
#include <tx/paging.h>

union __aligned(0x8000) kernel_stack {
    struct proc *proc;
    byte stack[0x8000];
};

typedef u32 pid_t;

struct context {
    u64 rsp;
    // TODO: Make context more complete.
};

struct proc {
    struct vas vas;
    union kernel_stack *kstack;
    struct trap_frame *trap_frame;
    struct context *context;
    pid_t pid;
};

struct proc *proc_create(struct bytes exec);

#endif // __TX_PROC_H__
