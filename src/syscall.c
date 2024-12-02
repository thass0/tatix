#include <tx/print.h>
#include <tx/syscall.h>

static int handle_syscall_write(i32 fd __unused, char *buf, sz len)
{
    if (!buf)
        return -1;
    if (len < 0)
        return -1;
    print_str(str_from(buf, len));
    return 0;
}

int handle_syscall(struct trap_frame *cpu_state)
{
    int ret = 0;
    switch (cpu_state->rax) {
    case SYSCALL_NUM_READ:
        break;
    case SYSCALL_NUM_WRITE:
        ret = handle_syscall_write(cpu_state->rdi, (char *)cpu_state->rsi, cpu_state->rdx);
        break;
    default:
        ret = -1;
        break;
    }
    cpu_state->rax = ret;
    return 0;
}
