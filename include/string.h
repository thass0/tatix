#ifndef _STRING_H_
#define _STRING_H_

#include <base.h>

struct str {
    char *dat;
    i64 len;
};

#define STR(s) \
    (struct str) { .dat = (s), .len = lengthof(s) }
#define STR_IS_NULL(s) ((s).dat == NULL)
#define RANGE_2_STR(beg, end) \
    (struct str) { .dat = (beg), .len = (end) - (beg) }

#endif // _STRING_H_
