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

#endif // __TX_STRING_H__
