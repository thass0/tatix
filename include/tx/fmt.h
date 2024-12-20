#ifndef __TX_FMT_H__
#define __TX_FMT_H__

#include <tx/base.h>
#include <tx/error.h>
#include <tx/stringdef.h>

// Formatting is based on: https://nullprogram.com/blog/2023/02/13/

// All functions in this file work by appending something to the buffer
// that they're given. `fmt` and `vfmt` are wrappers around the `append_*`
// functions. `fmt` and `vfmt` implement a printf-like syntax for convenience
// while the `append_*` functions implement formatting of different data types.

static inline struct result append_str(struct str s, struct str_buf *buf)
{
    if (!buf)
        return result_error(EINVAL);

    if (buf->cap < buf->len + s.len)
        return result_error(ENOMEM);

    for (sz i = 0; i < s.len; i++)
        (buf->dat + buf->len)[i] = s.dat[i];
    buf->len += s.len;

    return result_ok();
}

static inline struct result append_char(char ch, struct str_buf *buf)
{
    if (!buf)
        return result_error(EINVAL);

    // Capacity will be exceeded if we add one byte.
    if (buf->cap == buf->len)
        return result_error(ENOMEM);

    buf->dat[buf->len++] = ch;

    return result_ok();
}

static inline struct result append_i64(i64 x, struct str_buf *buf)
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

    return append_str(str_from_range(beg, end), buf);
}

static inline struct result append_u64(u64 x, struct str_buf *buf)
{
    if (!buf)
        return result_error(EINVAL);

    char tmp[64];
    char *end = tmp + countof(tmp);
    char *beg = end;

    do {
        *(--beg) = '0' + (x % 10);
    } while (x /= 10);

    return append_str(str_from_range(beg, end), buf);
}

enum fmt_hex_alpha { HEX_ALPHA_UPPER, HEX_ALPHA_LOWER };

static inline struct result append_hex(u64 x, enum fmt_hex_alpha alpha, struct str_buf *buf)
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

    return append_str(str_from_range(beg, end), buf);
}

static inline struct result append_ptr(void *p, struct str_buf *buf)
{
    return append_hex((u64)p, HEX_ALPHA_LOWER, buf);
}

static inline struct result vfmt(struct str_buf *buf, struct str fmt, va_list argp)
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
            append_char(fmt.dat[i], buf);
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
                    res = append_i64((i64)va_arg(argp, i64), buf);
                if (modifier == H)
                    res = append_i64((i64)va_arg(argp, i32) & 0xffff, buf);
                if (modifier == HH)
                    res = append_i64((i64)va_arg(argp, i32) & 0xff, buf);
                if (modifier == NONE)
                    res = append_i64((i64)va_arg(argp, i32), buf);
                modifier = NONE;
                break;
            case 'u':
                if (modifier == L)
                    res = append_u64((u64)va_arg(argp, u64), buf);
                if (modifier == H)
                    res = append_u64((u64)va_arg(argp, u32) & 0xffff, buf);
                if (modifier == HH)
                    res = append_u64((u64)va_arg(argp, u32) & 0xff, buf);
                if (modifier == NONE)
                    res = append_u64((u64)va_arg(argp, u32), buf);
                modifier = NONE;
                break;
            case 'x':
                if (modifier == L)
                    res = append_hex((u64)va_arg(argp, u64), HEX_ALPHA_LOWER, buf);
                if (modifier == H)
                    res = append_hex((u64)va_arg(argp, u32) & 0xffff, HEX_ALPHA_LOWER, buf);
                if (modifier == HH)
                    res = append_hex((u64)va_arg(argp, u32) & 0xff, HEX_ALPHA_LOWER, buf);
                if (modifier == NONE)
                    res = append_hex((u64)va_arg(argp, u32), HEX_ALPHA_LOWER, buf);
                modifier = NONE;
                break;
            case 'X':
                if (modifier == L)
                    res = append_hex((u64)va_arg(argp, u64), HEX_ALPHA_UPPER, buf);
                if (modifier == H)
                    res = append_hex((u64)va_arg(argp, u32) & 0xffff, HEX_ALPHA_UPPER, buf);
                if (modifier == HH)
                    res = append_hex((u64)va_arg(argp, u32) & 0xff, HEX_ALPHA_UPPER, buf);
                if (modifier == NONE)
                    res = append_hex((u64)va_arg(argp, u32), HEX_ALPHA_UPPER, buf);
                modifier = NONE;
                break;
            case 's': {
                struct str s = va_arg(argp, struct str);
                if (!STR_IS_NULL(s))
                    res = append_str(s, buf);
                else
                    res = append_str(STR("(NULL)"), buf);
                modifier = NONE;
                break;
            }
            case 'c':
                res = append_char((char)va_arg(argp, i32) & 0xff, buf);
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
    res = vfmt(buf, fmt, argp);
    va_end(argp);
    return res;
}

#endif // __TX_FMT_H__
