#ifndef _STRING_H_
#define _STRING_H_

#include <base.h>

struct str {
    char* dat;
    i64 len;
};

#define STR(s) \
    (struct str) { .dat = (s), .len = sizeof(s) - 1 }
#define STR_IS_NULL(s) ((s).dat == NULL)

#endif // _STRING_H_
