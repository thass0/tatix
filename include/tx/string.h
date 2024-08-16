#ifndef __TX_STRING_H__
#define __TX_STRING_H__

#include <tx/base.h>

struct str {
    char *dat;
    i64 len;
};

#define STR(s)                         \
    (struct str)                       \
    {                                  \
        .dat = (s), .len = lengthof(s) \
    }
#define STR_IS_NULL(s) ((s).dat == NULL)
#define RANGE_2_STR(beg, end)              \
    (struct str)                           \
    {                                      \
        .dat = (beg), .len = (end) - (beg) \
    }

#endif // __TX_STRING_H__
