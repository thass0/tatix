#include <tx/base.h>
#include <tx/error.h>
#include <tx/string.h>

struct result_sz syscall_write(sz fd, struct str s)
{
    sz n_written = 0;

    __asm__ volatile("mov $1, %%rax\n\t"
                     "mov %1, %%rdi\n\t"
                     "mov %2, %%rsi\n\t"
                     "mov %3, %%rdx\n\t"
                     "int $0x80\n\t"
                     "mov %%rax, %0\n\t"
                     : "=r"(n_written)
                     : "r"(fd), "r"(s.dat), "r"(s.len)
                     : "rax", "rdi", "rsi", "rdx");

    if (n_written < 0)
        return result_sz_error(-n_written);

    return result_sz_ok(n_written);
}

struct result_sz syscall_read(sz fd, struct str_buf *sbuf)
{
    sz n_read = 0;

    __asm__ volatile("mov $0, %%rax\n\t"
                     "mov %1, %%rdi\n\t"
                     "mov %2, %%rsi\n\t"
                     "mov %3, %%rdx\n\t"
                     "int $0x80\n\t"
                     "mov %%rax, %0\n\t"
                     : "=r"(n_read)
                     : "r"(fd), "r"(sbuf->dat + sbuf->len), "r"(sbuf->cap - sbuf->len)
                     : "rax", "rdi", "rsi", "rdx", "memory");

    if (n_read < 0)
        return result_sz_error(-n_read);

    sbuf->len += MIN(n_read, sbuf->cap - sbuf->len);
    return result_sz_ok(n_read);
}

char buf[256];

int main(void)
{
    struct result_sz res = syscall_write(0, STR("Hello from userspace! Please enter your name:\n"));
    if (res.is_error)
        return res.code;
    struct str_buf sbuf = str_buf_new(buf, 0, countof(buf));
    res = syscall_read(0, &sbuf);
    if (res.is_error)
        return res.code;
    res = syscall_write(0, STR("Your name is: "));
    if (res.is_error)
        return res.code;
    res = syscall_write(0, str_from_buf(sbuf));
    if (res.is_error)
        return res.code;
    return 0;
}
