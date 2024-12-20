#include <tx/com.h>
#include <tx/error.h>
#include <tx/fmt.h>
#include <tx/print.h>
#include <tx/syscall.h>

static struct result handle_syscall_write(i32 fd __unused, char *buf, sz len)
{
    if (!buf)
        return result_error(EINVAL);
    if (len < 0)
        return result_error(EINVAL);
    return print_str(str_new(buf, len));
}

static struct result handle_syscall_read(i32 fd __unused, char *buf, sz len)
{
    if (!buf)
        return result_error(EINVAL);
    if (len < 0)
        return result_error(EINVAL);

    // Nothing to read in this case:
    if (!len)
        return result_ok();

    struct result res = result_ok();
    struct str_buf sbuf = str_buf_new(buf, 0, len);
    char backing = '\0';
    struct str_buf tmp_buf = str_buf_new(&backing, 0, 1);

    do {
        res = com_read(COM1_PORT, &tmp_buf);
        if (res.is_error)
            return res;
        res = com_write(COM1_PORT, str_from_buf(tmp_buf));
        if (res.is_error)
            return res;
        if (backing == '\r') {
            res = com_write(COM1_PORT, STR("\n"));
            if (res.is_error)
                return res;
        }
        res = append_char(backing, &sbuf);
        if (res.is_error)
            return res;
        str_buf_clear(&tmp_buf);
    } while (backing != '\n' && backing != '\r');

    return res;
}

void handle_syscall(struct trap_frame *cpu_state)
{
    struct result res = result_ok();
    switch (cpu_state->rax) {
    case SYSCALL_NUM_READ:
        res = handle_syscall_read(cpu_state->rdi, (char *)cpu_state->rsi, cpu_state->rdx);
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
