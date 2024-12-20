#include <tx/com.h>
#include <tx/error.h>
#include <tx/fmt.h>
#include <tx/print.h>
#include <tx/string.h>
#include <tx/syscall.h>

static struct result_sz handle_syscall_write(i32 fd __unused, char *buf, sz len)
{
    if (!buf)
        return result_sz_error(EINVAL);
    if (len < 0)
        return result_sz_error(EINVAL);
    struct result res = print_str(str_new(buf, len));
    if (res.is_error)
        return result_sz_from_error(res);
    return result_sz_ok(len);
}

static struct result_sz handle_syscall_read(i32 fd __unused, char *buf, sz len)
{
    if (!buf)
        return result_sz_error(EINVAL);
    if (len < 0)
        return result_sz_error(EINVAL);

    // Nothing to read in this case:
    if (!len)
        return result_sz_ok(0);

    struct result res = result_ok();
    struct str_buf sbuf = str_buf_new(buf, 0, len);
    char backing = '\0';
    struct str_buf tmp_buf = str_buf_new(&backing, 0, 1);

    do {
        res = com_read(COM1_PORT, &tmp_buf);
        if (res.is_error)
            return result_sz_from_error(res);
        if (backing == '\r') {
            res = com_write(COM1_PORT, STR("\n"));
            if (res.is_error)
                return result_sz_from_error(res);
            res = append_char('\n', &sbuf);
            if (res.is_error)
                return result_sz_from_error(res);
        }
        if (backing == (char)127) { // Del
            if (sbuf.len) {
                // Only delete the characters from the screen that the caller wrote before
                res = com_write(COM1_PORT, STR("\b \b"));
                if (res.is_error)
                    return result_sz_from_error(res);
                str_buf_pop(&sbuf);
            }
            continue;
        }
        res = com_write(COM1_PORT, str_from_buf(tmp_buf));
        if (res.is_error)
            return result_sz_from_error(res);
        res = append_char(backing, &sbuf);
        if (res.is_error)
            return result_sz_from_error(res);
        str_buf_clear(&tmp_buf);
    } while (backing != '\n' && backing != '\r');

    return result_sz_ok(sbuf.len);
}

void handle_syscall(struct trap_frame *cpu_state)
{
    struct result_sz res = result_sz_ok(0);
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
        cpu_state->rax = -(sz)res.code;
    else
        cpu_state->rax = result_sz_checked(res);
}
