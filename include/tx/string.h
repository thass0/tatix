#ifndef __TX_STRING_H__
#define __TX_STRING_H__

#include <tx/assert.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/option.h>
#include <tx/stringdef.h>

// TODO: Ideally, we would like to have safe unicode strings. Then, there would need to be checks that make
// sure these functions are safe:

static inline struct str_buf str_buf_from_byte_array(struct byte_array ba)
{
    return str_buf_new((char *)ba.dat, 0, ba.len);
}

static inline struct str_buf str_buf_from_byte_buf(struct byte_buf bb)
{
    return str_buf_new((char *)bb.dat, bb.len, bb.cap);
}

static inline struct str str_from_byte_buf(struct byte_buf bb)
{
    return str_new((char *)bb.dat, bb.len);
}

static inline bool str_is_equal(struct str a, struct str b)
{
    bool not_equal = false;
    if (a.len != b.len)
        return false;
    while (a.len--) {
        not_equal |= *a.dat != *b.dat;
        a.dat++;
        b.dat++;
    }
    return !not_equal;
}

static inline struct option_sz str_find_char(struct str s, char ch)
{
    sz l = s.len;
    while (s.len) {
        if (*s.dat++ == ch)
            return option_sz_ok(l - s.len);
        s.len--;
    }
    return option_sz_none();
}

static inline struct result str_buf_append(struct str_buf *buf, struct str s)
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

static inline struct result str_buf_append_char(struct str_buf *buf, char ch)
{
    if (!buf)
        return result_error(EINVAL);

    // Capacity will be exceeded if we add one byte.
    if (buf->cap == buf->len)
        return result_error(ENOMEM);

    buf->dat[buf->len++] = ch;

    return result_ok();
}

#endif // __TX_STRING_H__
