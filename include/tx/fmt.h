#ifndef __TX_FMT_H__
#define __TX_FMT_H__

#include <tx/arena.h>
#include <tx/base.h>
#include <tx/string.h>

static inline struct str_buf fmt_buf_new(struct arena *arn, sz cap)
{
    struct str_buf buf;
    buf.dat = arena_alloc_array(arn, cap, sizeof(*buf.dat));
    buf.len = 0;
    buf.cap = cap;
    return buf;
}

__printf(2, 3) int fmt(struct str_buf *buf, const char *fmt, ...);

#endif // __TX_FMT_H__
