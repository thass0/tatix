#ifndef __TX_STRING_H__
#define __TX_STRING_H__

#include <tx/assert.h>
#include <tx/stringdef.h>

static inline char str_buf_get_checked(struct str_buf *buf, sz idx)
{
    assert(buf);
    assert(idx >= 0);
    assert(idx < buf->len);
    return buf->dat[idx];
}

static inline void str_buf_pop(struct str_buf *buf)
{
    assert(buf);
    if (buf->len > 0)
        buf->len--;
}

#endif // __TX_STRING_H__
