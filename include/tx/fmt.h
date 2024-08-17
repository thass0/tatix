#ifndef __TX_FMT_H__
#define __TX_FMT_H__

#include <tx/base.h>
#include <tx/string.h>

__printf(2, 3) int fmt(struct str_buf *buf, const char *fmt, ...);

#endif // __TX_FMT_H__
