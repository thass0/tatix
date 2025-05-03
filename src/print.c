// Print textual output

#include <tx/base.h>
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

#define PRINT_DBG_BUF_SIZE 700

// These are determined by examining all files during the build process and filled-in by the linker when linking the
// final binary. They allow neatly aligning the debug output.
extern char BASENAME_MAX_LEN[];
static sz basename_max_len = (sz)BASENAME_MAX_LEN;
extern char LINE_MAX_LEN[];
static sz line_max_len = (sz)LINE_MAX_LEN;
extern char FUNCNAME_MAX_LEN[];
static sz funcname_max_len = (sz)FUNCNAME_MAX_LEN;

struct result __print_dbg(struct str basename, struct str line, struct str funcname, i16 level, struct str fmt_str, ...)
{
    if (level > __DEBUG__)
        return result_ok();

    char underlying[PRINT_DBG_BUF_SIZE];
    struct str_buf buf = str_buf_new(underlying, 0, countof(underlying));
    struct str_buf buf_cpy = buf;
    struct result res = result_ok();

    va_list argp;
    va_start(argp, fmt_str);

    res.is_error |= str_buf_append_char(&buf_cpy, '[').is_error;
    res.is_error |= str_buf_append(&buf_cpy, basename).is_error;
    res.is_error |= str_buf_append_char(&buf_cpy, ':').is_error;
    res.is_error |= str_buf_append(&buf_cpy, line).is_error;

    // Add padding to compensate for the different numbers of characters in base names and line lengths.
    res.is_error |= str_buf_append_n(&buf_cpy, basename_max_len - basename.len, ' ').is_error;
    res.is_error |= str_buf_append_n(&buf_cpy, line_max_len - line.len, ' ').is_error;
    res.is_error |= str_buf_append(&buf_cpy, STR(" | ")).is_error;

    res.is_error |= str_buf_append(&buf_cpy, funcname).is_error;
    res.is_error |= str_buf_append_n(&buf_cpy, funcname_max_len - funcname.len, ' ').is_error;

    res.is_error |= str_buf_append(&buf_cpy, STR("]: ")).is_error;

    if (!res.is_error) {
        buf = buf_cpy;
    } else {
        print_str(STR("ERROR: print_dbg failed to format source code location\n"));
        return result_error(ENOMEM);
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
