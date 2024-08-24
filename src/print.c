// Print textual output

#include <tx/com.h>
#include <tx/fmt.h>
#include <tx/print.h>

int print_str(struct str str)
{
    return com_write(COM1_PORT, str);
}

int print_fmt(struct str_buf buf, struct str fmt, ...)
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

int print(struct str fmt, ...)
{
    va_list argp;
    int rc;
    char underlying[128];
    struct str_buf buf = str_buf_new(underlying, 0, countof(underlying));
    va_start(argp, fmt);
    rc = vfmt(&buf, fmt, argp);
    if (rc < 0)
        return rc;
    rc = print_str(str_from_buf(buf));
    va_end(argp);
    return rc;
}
