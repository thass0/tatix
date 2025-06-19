#ifndef __TX_STRING_H__
#define __TX_STRING_H__

#include <tx/assert.h>
#include <tx/base.h>
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

static inline struct str str_from_byte_view(struct byte_view bv)
{
    return str_new((char *)bv.dat, bv.len);
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

static inline bool str_has_prefix(struct str str, struct str prefix)
{
    if (str.len < prefix.len)
        return false;
    struct str must_match = str_new(str.dat, prefix.len);
    return str_is_equal(must_match, prefix);
}

static inline bool str_consume_prefix(struct str *str, struct str prefix)
{
    assert(str);
    if (!str_has_prefix(*str, prefix))
        return false;
    str->dat += prefix.len;
    str->len -= prefix.len;
    return true;
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

static inline struct option_sz str_find_char_reverse(struct str s, char ch)
{
    for (sz i = s.len; i > 0; i--) {
        if (s.dat[i - 1] == ch)
            return option_sz_ok(i - 1);
    }
    return option_sz_none();
}

static inline struct option_sz str_find_substring(struct str search, struct str substr)
{
    assert(substr.len > 0);

    sz total_offset = 0;

    while (search.len >= substr.len) {
        struct option_sz next_opt = str_find_char(search, substr.dat[0]);
        if (next_opt.is_none)
            return option_sz_none();

        sz next = option_sz_checked(next_opt);
        assert(next < search.len);

        search.dat += next;
        search.len -= next;
        total_offset += next;

        if (search.len < substr.len)
            return option_sz_none();

        // This is ok since search.len >= substr.len.
        if (str_is_equal(str_new(search.dat, substr.len), substr))
            return option_sz_ok(total_offset);

        search.dat++;
        search.len--;
        total_offset++;
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

static inline struct result str_buf_append_n(struct str_buf *buf, sz n, char ch)
{
    if (!buf)
        return result_error(EINVAL);

    // Capacity will be exceeded if we add n bytes.
    if (buf->cap < buf->len + n)
        return result_error(ENOMEM);

    for (sz i = 0; i < n; i++)
        (buf->dat + buf->len)[i] = ch;
    buf->len += n;

    return result_ok();
}

#endif // __TX_STRING_H__
