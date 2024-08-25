#ifndef __TX_PRINT_H__
#define __TX_PRINT_H__

#include <tx/base.h>
#include <tx/string.h>

int print_str(struct str str);
int print_fmt(struct str_buf buf, struct str fmt, ...);

#if __DEBUG__ >= 1
int __print_dbg(struct str basename, sz lineno, struct str funcname, struct str fmt, ...);
#define print_dbg(fmt, ...)                  \
    __print_dbg(STR(__BASENAME__), __LINE__, \
                (struct str){ .dat = DECONST(char *, __func__), .len = lengthof(__func__) }, fmt, ##__VA_ARGS__)
#else
#define print_dbg(fmt, ...)
#endif

#endif // __TX_PRINT_H__
