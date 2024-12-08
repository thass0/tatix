#ifndef __TX_SYSCALL_H__
#define __TX_SYSCALL_H__

#include <tx/isr.h>

#define SYSCALL_NUM_READ 0
#define SYSCALL_NUM_WRITE 1

void handle_syscall(struct trap_frame *cpu_state);

#endif // __TX_SYSCALL_H__
