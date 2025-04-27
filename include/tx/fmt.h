#ifndef __TX_FMT_H__
#define __TX_FMT_H__

#include <tx/base.h>
#include <tx/errordef.h>
#include <tx/string.h>

// Formatting is based on: https://nullprogram.com/blog/2023/02/13/

// All functions in this file work by appending something to the buffer
// that they're given. `fmt` and `vfmt` are wrappers around the `append_*`
// functions. `fmt` and `vfmt` implement a printf-like syntax for convenience
// while the `append_*` functions implement formatting of different data types.

static inline struct result fmt_append_i64(i64 x, struct str_buf *buf)
{
    if (!buf)
        return result_error(EINVAL);

    char tmp[64];
    char *end = tmp + countof(tmp);
    char *beg = end;

    i64 d = x < 0 ? -x : x;
    do {
        *(--beg) = '0' + (d % 10);
    } while (d /= 10);

    if (x < 0) {
        *(--beg) = '-';
    }

    return str_buf_append(buf, str_from_range(beg, end));
}

static inline struct result fmt_append_u64(u64 x, struct str_buf *buf)
{
    if (!buf)
        return result_error(EINVAL);

    char tmp[64];
    char *end = tmp + countof(tmp);
    char *beg = end;

    do {
        *(--beg) = '0' + (x % 10);
    } while (x /= 10);

    return str_buf_append(buf, str_from_range(beg, end));
}

enum fmt_hex_alpha { HEX_ALPHA_UPPER, HEX_ALPHA_LOWER };

static inline struct result fmt_append_hex(u64 x, enum fmt_hex_alpha alpha, struct str_buf *buf)
{
    if (!buf)
        return result_error(EINVAL);

    char tmp[64];
    char *end = tmp + countof(tmp);
    char *beg = end;

    const char *a;
    switch (alpha) {
    case HEX_ALPHA_LOWER:
        a = "0123456789abcdef";
        break;
    case HEX_ALPHA_UPPER:
        a = "0123456789ABCDEF";
        break;
    }

    do {
        *--beg = a[x & 0xf];
    } while (x >>= 4);

    return str_buf_append(buf, str_from_range(beg, end));
}

static inline struct result fmt_append_ptr(void *p, struct str_buf *buf)
{
    return fmt_append_hex((u64)p, HEX_ALPHA_LOWER, buf);
}

static inline struct result fmt_vfmt(struct str_buf *buf, struct str fmt, va_list argp)
{
    enum { NONE, L, HH, H } modifier = NONE;
    enum { NOTHING, MODIFIER, CONVERSION } expect = NOTHING;
    struct result res = result_ok();
    sz i = 0;

    if (!buf || !buf->dat || !fmt.dat || fmt.len < 0)
        return result_error(EINVAL);

    while (i < fmt.len) {
        if (expect == NOTHING && fmt.dat[i] == '%') {
            expect = MODIFIER;
            i++;
            continue;
        }

        if (expect == NOTHING) {
            res = str_buf_append_char(buf, fmt.dat[i]);
            if (res.is_error)
                return res;
            i++;
            continue;
        }

        if (expect == MODIFIER) {
            switch (fmt.dat[i]) {
            case 'l':
                modifier = L;
                if (fmt.dat[i + 1] == 'l')
                    i += 2;
                else
                    i++;
                break;
            case 'h':
                if (fmt.dat[i + 1] == 'h') {
                    modifier = HH;
                    i += 2;
                } else {
                    modifier = H;
                    i++;
                }
                break;
            default:
                break;
            }

            expect = CONVERSION;
            continue;
        }

        if (expect == CONVERSION) {
            switch (fmt.dat[i]) {
            case 'i':
            case 'd':
                if (modifier == L)
                    res = fmt_append_i64((i64)va_arg(argp, i64), buf);
                if (modifier == H)
                    res = fmt_append_i64((i64)va_arg(argp, i32) & 0xffff, buf);
                if (modifier == HH)
                    res = fmt_append_i64((i64)va_arg(argp, i32) & 0xff, buf);
                if (modifier == NONE)
                    res = fmt_append_i64((i64)va_arg(argp, i32), buf);
                modifier = NONE;
                break;
            case 'u':
                if (modifier == L)
                    res = fmt_append_u64((u64)va_arg(argp, u64), buf);
                if (modifier == H)
                    res = fmt_append_u64((u64)va_arg(argp, u32) & 0xffff, buf);
                if (modifier == HH)
                    res = fmt_append_u64((u64)va_arg(argp, u32) & 0xff, buf);
                if (modifier == NONE)
                    res = fmt_append_u64((u64)va_arg(argp, u32), buf);
                modifier = NONE;
                break;
            case 'x':
                if (modifier == L)
                    res = fmt_append_hex((u64)va_arg(argp, u64), HEX_ALPHA_LOWER, buf);
                if (modifier == H)
                    res = fmt_append_hex((u64)va_arg(argp, u32) & 0xffff, HEX_ALPHA_LOWER, buf);
                if (modifier == HH)
                    res = fmt_append_hex((u64)va_arg(argp, u32) & 0xff, HEX_ALPHA_LOWER, buf);
                if (modifier == NONE)
                    res = fmt_append_hex((u64)va_arg(argp, u32), HEX_ALPHA_LOWER, buf);
                modifier = NONE;
                break;
            case 'X':
                if (modifier == L)
                    res = fmt_append_hex((u64)va_arg(argp, u64), HEX_ALPHA_UPPER, buf);
                if (modifier == H)
                    res = fmt_append_hex((u64)va_arg(argp, u32) & 0xffff, HEX_ALPHA_UPPER, buf);
                if (modifier == HH)
                    res = fmt_append_hex((u64)va_arg(argp, u32) & 0xff, HEX_ALPHA_UPPER, buf);
                if (modifier == NONE)
                    res = fmt_append_hex((u64)va_arg(argp, u32), HEX_ALPHA_UPPER, buf);
                modifier = NONE;
                break;
            case 's': {
                struct str s = va_arg(argp, struct str);
                if (s.dat && s.len >= 0)
                    res = str_buf_append(buf, s);
                else
                    res = str_buf_append(buf, STR("(NULL)"));
                modifier = NONE;
                break;
            }
            case 'c':
                res = str_buf_append_char(buf, (char)va_arg(argp, i32) & 0xff);
                modifier = NONE;
                break;
            default:
                res = result_error(EINVAL);
                break;
            }

            if (res.is_error)
                return res;

            expect = NOTHING;
            i++;
            continue;
        }
    }

    return res;
}

static inline struct result fmt(struct str_buf *buf, struct str fmt, ...)
{
    va_list argp;
    struct result res = result_ok();
    va_start(argp, fmt);
    res = fmt_vfmt(buf, fmt, argp);
    va_end(argp);
    return res;
}

#endif // __TX_FMT_H__
