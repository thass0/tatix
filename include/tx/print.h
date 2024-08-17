#ifndef __TX_PRINT_H__
#define __TX_PRINT_H__

#include <tx/base.h>
#include <tx/string.h>

int print_str(struct str str);
__printf(2, 3) int print_fmt(struct str_buf buf, const char *fmt, ...);

#endif // __TX_PRINT_H__
