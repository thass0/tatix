// Print textual output

#include <tx/com.h>
#include <tx/fmt.h>
#include <tx/print.h>

struct result print_str(struct str str)
{
    return com_write(COM1_PORT, str);
}

struct result print_fmt(struct str_buf buf, struct str fmt, ...)
{
    va_list argp;
    struct result res = result_ok();
    va_start(argp, fmt);
    res = fmt_vfmt(&buf, fmt, argp);
    if (res.is_error)
        return res;
    res = com_write(COM1_PORT, str_from_buf(buf));
    va_end(argp);
    return res;
}

#if __DEBUG__ >= 1

#define PRINT_DBG_BUF_SIZE 700

struct result __print_dbg(struct str basename, sz line, struct str funcname, struct str fmt_str, ...)
{
    va_list argp;
    struct result res = result_ok();
    char underlying[PRINT_DBG_BUF_SIZE];
    struct str_buf buf = str_buf_new(underlying, 0, countof(underlying));
    struct str_buf buf_cpy = buf;
    va_start(argp, fmt_str);
    res = fmt(&buf_cpy, STR("%s %s:%ld "), funcname, basename, line);
    if (!res.is_error) {
        buf = buf_cpy;
    } else {
        print_str(STR("ERROR: print_dbg failed to format source code location\n"));
        return res;
    }
    res = fmt_vfmt(&buf_cpy, fmt_str, argp);
    if (!res.is_error) {
        buf = buf_cpy;
    } else {
        print_str(STR("ERROR: print_dbg failed to format debug message, source code location is: "));
        print_str(str_from_buf(buf));
        print_str(STR("\n"));
        return res;
    }
    res = print_str(str_from_buf(buf));
    va_end(argp);
    return res;
}
#endif
