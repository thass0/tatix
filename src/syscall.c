#include <tx/error.h>
#include <tx/print.h>
#include <tx/syscall.h>

static struct result handle_syscall_write(i32 fd __unused, char *buf, sz len)
{
    if (!buf)
        return result_error(EINVAL);
    if (len < 0)
        return result_error(EINVAL);
    return print_str(str_from(buf, len));
}

void handle_syscall(struct trap_frame *cpu_state)
{
    struct result res = result_ok();
    switch (cpu_state->rax) {
    case SYSCALL_NUM_READ:
        break;
    case SYSCALL_NUM_WRITE:
        res = handle_syscall_write(cpu_state->rdi, (char *)cpu_state->rsi, cpu_state->rdx);
        break;
    default:
        break;
    }

    if (res.is_error)
        cpu_state->rax = res.code;
    else
        cpu_state->rax = 0;
}
