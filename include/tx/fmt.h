#ifndef __TX_FMT_H__
#define __TX_FMT_H__

#include <tx/base.h>
#include <tx/string.h>

int fmt(struct str_buf *buf, struct str fmt, ...);
int vfmt(struct str_buf *buf, struct str fmt, va_list argp);

#endif // __TX_FMT_H__
