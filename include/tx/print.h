#ifndef __TX_PRINT_H__
#define __TX_PRINT_H__

#include <tx/base.h>
#include <tx/errordef.h>
#include <tx/stringdef.h>

struct result print_str(struct str str);
struct result print_fmt(struct str_buf buf, struct str fmt, ...);

#define PDBG 2
#define PINFO 1
#define PWARN 0
#define PERROR 0

struct result __print_dbg(struct str basename, sz lineno, struct str funcname, i16 level, struct str fmt, ...);
#define print_dbg(level, fmt, ...)                                                                       \
    __print_dbg(STR(__BASENAME__), __LINE__,                                                             \
                (struct str){ .dat = DECONST(char *, __func__), .len = lengthof(__func__) }, level, fmt, \
                ##__VA_ARGS__)

#endif // __TX_PRINT_H__
