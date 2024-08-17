#include <tx/fmt.h>
#include <tx/string.h>

// Formatting is based on: https://nullprogram.com/blog/2023/02/13/

int append_str(struct str s, struct str_buf *buf)
{
    if (!buf)
        return -1;

    if (buf->cap < buf->len + s.len)
        return -1;

    for (sz i = 0; i < s.len; i++)
        (buf->dat + buf->len)[i] = s.dat[i];
    buf->len += s.len;

    return 0;
}

int append_char(char ch, struct str_buf *buf)
{
    if (!buf)
        return -1;

    // Capacity will be exceeded if we add one byte.
    if (buf->cap == buf->len)
        return -1;

    buf->dat[buf->len++] = ch;

    return 0;
}

int append_i64(i64 x, struct str_buf *buf)
{
    if (!buf)
        return -1;

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

int append_u64(u64 x, struct str_buf *buf)
{
    if (!buf)
        return -1;

    char tmp[64];
    char *end = tmp + countof(tmp);
    char *beg = end;

    do {
        *(--beg) = '0' + (x % 10);
    } while (x /= 10);

    return append_str(str_from_range(beg, end), buf);
}

enum hex_alpha { HEX_ALPHA_UPPER, HEX_ALPHA_LOWER };

int append_hex(u64 x, enum hex_alpha alpha, struct str_buf *buf)
{
    if (!buf)
        return -1;

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

int append_ptr(void *p, struct str_buf *buf)
{
    return append_hex((u64)p, HEX_ALPHA_LOWER, buf);
}

__printf(2, 3) int fmt(struct str_buf *buf, const char *fmt, ...)
{
    va_list argp;
    enum { NONE, L, HH, H } modifier = NONE;
    enum { NOTHING, MODIFIER, CONVERSION } expect = NOTHING;
    int rc = 0;

    if (!buf || !buf->dat || !fmt)
        return -1;

    va_start(argp, fmt);
    while (*fmt) {
        if (expect == NOTHING && *fmt == '%') {
            expect = MODIFIER;
            fmt++;
            continue;
        }

        if (expect == NOTHING) {
            append_char(*fmt, buf);
            fmt++;
            continue;
        }

        if (expect == MODIFIER) {
            switch (*fmt) {
            case 'l':
                modifier = L;
                if (*(fmt + 1) == 'l')
                    fmt += 2;
                else
                    fmt++;
                break;
            case 'h':
                if (*(fmt + 1) == 'h') {
                    modifier = HH;
                    fmt += 2;
                } else {
                    modifier = H;
                    fmt++;
                }
                break;
            default:
                break;
            }

            expect = CONVERSION;
            continue;
        }

        if (expect == CONVERSION) {
            switch (*fmt) {
            case 'i':
            case 'd':
                if (modifier == L)
                    rc = append_i64((i64)va_arg(argp, i64), buf);
                if (modifier == H)
                    rc = append_i64((i64)va_arg(argp, i32) & 0xffff, buf);
                if (modifier == HH)
                    rc = append_i64((i64)va_arg(argp, i32) & 0xff, buf);
                if (modifier == NONE)
                    rc = append_i64((i64)va_arg(argp, i32), buf);
                modifier = NONE;
                break;
            case 'u':
                if (modifier == L)
                    rc = append_u64((u64)va_arg(argp, u64), buf);
                if (modifier == H)
                    rc = append_u64((u64)va_arg(argp, u32) & 0xffff, buf);
                if (modifier == HH)
                    rc = append_u64((u64)va_arg(argp, u32) & 0xff, buf);
                if (modifier == NONE)
                    rc = append_u64((u64)va_arg(argp, u32), buf);
                modifier = NONE;
                break;
            case 'x':
                if (modifier == L)
                    rc = append_hex((u64)va_arg(argp, u64), HEX_ALPHA_LOWER, buf);
                if (modifier == H)
                    rc = append_hex((u64)va_arg(argp, u32) & 0xffff, HEX_ALPHA_LOWER, buf);
                if (modifier == HH)
                    rc = append_hex((u64)va_arg(argp, u32) & 0xff, HEX_ALPHA_LOWER, buf);
                if (modifier == NONE)
                    rc = append_hex((u64)va_arg(argp, u32), HEX_ALPHA_LOWER, buf);
                modifier = NONE;
                break;
            case 'X':
                if (modifier == L)
                    rc = append_hex((u64)va_arg(argp, u64), HEX_ALPHA_UPPER, buf);
                if (modifier == H)
                    rc = append_hex((u64)va_arg(argp, u32) & 0xffff, HEX_ALPHA_UPPER, buf);
                if (modifier == HH)
                    rc = append_hex((u64)va_arg(argp, u32) & 0xff, HEX_ALPHA_UPPER, buf);
                if (modifier == NONE)
                    rc = append_hex((u64)va_arg(argp, u32), HEX_ALPHA_UPPER, buf);
                modifier = NONE;
                break;
            case 's': {
                struct str s = va_arg(argp, struct str);
                if (!STR_IS_NULL(s))
                    rc = append_str(s, buf);
                else
                    rc = append_str(STR("(NULL)"), buf);
                modifier = NONE;
                break;
            }
            default:
                break;
            }

            if (rc < 0)
                goto exit;

            expect = NOTHING;
            fmt++;
            continue;
        }
    }

exit:
    va_end(argp);
    return rc;
}
