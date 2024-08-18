#ifndef __TX_PRINT_H__
#define __TX_PRINT_H__

#include <tx/base.h>
#include <tx/string.h>

int print_str(struct str str);
int print_fmt(struct str_buf buf, struct str fmt, ...);

#endif // __TX_PRINT_H__
