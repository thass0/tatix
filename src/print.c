// Print textual output

#include <tx/com.h>
#include <tx/fmt.h>
#include <tx/print.h>

int print_str(struct str str)
{
    return com_write(COM1_PORT, str);
}

__printf(2, 3) int print_fmt(struct str_buf buf, const char *fmt, ...)
{
    va_list argp;
    int rc;
    va_start(argp, fmt);
    rc = vfmt(&buf, fmt, argp);
    if (rc < 0)
        return rc;
    rc = com_write(COM1_PORT, str_from_buf(buf));
    va_end(argp);
    return rc;
}
