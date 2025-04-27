// Definitions of string types. See string.h for more functions related to these. The
// reason for the split is that asserts and dependencies of assert should use these
// types, but the functions in string.h also use asserts so we need to avoid recursive
// calls (and dependencies).

#ifndef __TX_STRINGDEF_H__
#define __TX_STRINGDEF_H__

#include <tx/base.h>

struct str {
    char *dat;
    sz len;
};

struct str_buf {
    char *dat;
    sz len;
    sz cap;
};

#define STR(s)                         \
    (struct str)                       \
    {                                  \
        .dat = (s), .len = lengthof(s) \
    }

// NOTE: Compound literals (as used in `STR()`) can't be used as initializers for static variables (well, compilers
// are generally fine with it, but they complain). The issue is that a compound literals are not considered constant
// expressions. So use this any time using `STR()` gives you warnings.
#define STR_STATIC(s) { .dat = (s), .len = lengthof(s) }

static inline struct str_buf str_buf_new(char *dat, sz len, sz cap)
{
    struct str_buf buf;
    buf.dat = dat;
    buf.len = len;
    buf.cap = cap;
    return buf;
}

static inline void str_buf_clear(struct str_buf *buf)
{
    if (buf)
        buf->len = 0;
}

static inline struct str str_from_buf(struct str_buf buf)
{
    struct str str;
    str.dat = buf.dat;
    str.len = buf.len;
    return str;
}

static inline struct str str_from_range(char *beg, char *end)
{
    struct str str;
    str.dat = beg;
    str.len = end - beg;
    return str;
}

static inline struct str str_new(char *dat, sz len)
{
    struct str str;
    str.dat = dat;
    str.len = len;
    return str;
}

#endif // __TX_STRINGDEF_H__
