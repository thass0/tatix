#include <tx/assert.h>
#include <tx/fmt.h>
#include <tx/string.h>

// Formatting algorithms uses here: https://nullprogram.com/blog/2023/02/13/

#define IMPL_FMT_SIGNED(t)                                      \
    i32 fmt_##t(t x, struct fmt_buf *buf, struct arena scratch) \
    {                                                           \
        return fmt_i64((i64)x, buf, scratch);                   \
    }

IMPL_FMT_SIGNED(i8)
IMPL_FMT_SIGNED(i16)
IMPL_FMT_SIGNED(i32)

#if __PTRDIFF_WIDTH__ == 64
IMPL_FMT_SIGNED(sz)
#else
#error "Implementation to format size types depends on them being 64 bits wide"
#endif

i32 fmt_i64(i64 x, struct fmt_buf *buf, struct arena scratch)
{
    assert(buf != NULL);

    char *tmp = arena_alloc_array(&scratch, 32, sizeof(*tmp));
    char *end = tmp + 32;
    char *beg = end;

    if (tmp == NULL)
        return -1;

    i64 d = x < 0 ? -x : x;
    do {
        *(--beg) = '0' + (d % 10);
    } while (d /= 10);

    if (x < 0) {
        *(--beg) = '-';
    }

    return fmt_str(RANGE_2_STR(beg, end), buf);
}

#define IMPL_FMT_UNSIGNED(t)                                    \
    i32 fmt_##t(t x, struct fmt_buf *buf, struct arena scratch) \
    {                                                           \
        return fmt_u64((u64)x, buf, scratch);                   \
    }

IMPL_FMT_UNSIGNED(u8)
IMPL_FMT_UNSIGNED(u16)
IMPL_FMT_UNSIGNED(u32)

i32 fmt_u64(u64 x, struct fmt_buf *buf, struct arena scratch)
{
    assert(buf != NULL);

    char *tmp = arena_alloc_array(&scratch, 32, sizeof(*tmp));
    char *end = tmp + 32;
    char *beg = end;

    if (tmp == NULL)
        return -1;

    do {
        *(--beg) = '0' + (x % 10);
    } while (x /= 10);

    return fmt_str(RANGE_2_STR(beg, end), buf);
}

i32 fmt_hex(u64 x, struct fmt_buf *buf)
{
    assert(buf != NULL);

    if (fmt_str(STR("0x"), buf) < 0)
        return -1;

    for (sz i = 2 * sizeof(x) - 1; i >= 0; i--) {
        if (fmt_char("0123456789abcdef"[(x >> (4 * i)) & 15], buf) < 0)
            return -1;
    }

    return 0;
}

i32 fmt_ptr(void *p, struct fmt_buf *buf)
{
    return fmt_hex((u64)p, buf);
}

i32 fmt_str(struct str s, struct fmt_buf *buf)
{
    assert(buf != NULL);

    if (buf->cap < buf->len + s.len)
        return -1;

    for (sz i = 0; i < s.len; i++)
        (buf->dat + buf->len)[i] = s.dat[i];
    buf->len += s.len;

    return 0;
}

i32 fmt_char(char ch, struct fmt_buf *buf)
{
    assert(buf != NULL);

    // Capacity will be exceeded if we add one byte.
    if (buf->cap == buf->len)
        return -1;

    buf->dat[buf->len++] = ch;

    return 0;
}
