#ifndef _FMT_H_
#define _FMT_H_

#include <arena.h>
#include <base.h>
#include <string.h>

struct fmt_buf {
    char *dat;
    i64 len;
    i64 cap;
};

#define FMT_2_STR(b) \
    (struct str) { .dat = (b).dat, .len = (b).len }
#define NEW_FMT_BUF(arn, c) \
    (struct fmt_buf) { .dat = NEW((arn), char, (c)), .len = 0, .cap = (c) }
#define FMT_CLEAR(b)  \
    do {              \
        (b)->len = 0; \
    } while (0);

// Append the given value to the end of the formatting buffer.

i32 fmt_i8(i8, struct fmt_buf *, struct arena);
i32 fmt_i16(i16, struct fmt_buf *, struct arena);
i32 fmt_i32(i32, struct fmt_buf *, struct arena);
i32 fmt_i64(i64, struct fmt_buf *, struct arena);
i32 fmt_sz(sz, struct fmt_buf *, struct arena);
i32 fmt_u8(u8, struct fmt_buf *, struct arena);
i32 fmt_u16(u16, struct fmt_buf *, struct arena);
i32 fmt_u32(u32, struct fmt_buf *, struct arena);
i32 fmt_u64(u64, struct fmt_buf *, struct arena);
i32 fmt_ptr(void *, struct fmt_buf *);
i32 fmt_str(struct str, struct fmt_buf *);
i32 fmt_char(char, struct fmt_buf *);

#endif // _FMT_H_
