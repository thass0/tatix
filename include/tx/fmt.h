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

// Append the given value to the end of the formatting buffer.

i32 fmt_i8(i8, struct str_buf *, struct arena);
i32 fmt_i16(i16, struct str_buf *, struct arena);
i32 fmt_i32(i32, struct str_buf *, struct arena);
i32 fmt_i64(i64, struct str_buf *, struct arena);
i32 fmt_sz(sz, struct str_buf *, struct arena);
i32 fmt_u8(u8, struct str_buf *, struct arena);
i32 fmt_u16(u16, struct str_buf *, struct arena);
i32 fmt_u32(u32, struct str_buf *, struct arena);
i32 fmt_u64(u64, struct str_buf *, struct arena);
i32 fmt_hex(u64, struct str_buf *);
i32 fmt_ptr(void *, struct str_buf *);
i32 fmt_str(struct str, struct str_buf *);
i32 fmt_char(char, struct str_buf *);

#endif // __TX_FMT_H__
