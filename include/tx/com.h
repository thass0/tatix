#ifndef __TX_COM__
#define __TX_COM__

#include <tx/base.h>
#include <tx/error.h>
#include <tx/stringdef.h>

#define COM1_PORT 0x3f8
#define COM2_PORT 0x2f8
#define COM3_PORT 0x3e8
#define COM4_PORT 0x2e8

struct result com_init(u16 port);
struct result com_write(u16 port, struct str str);
struct result com_read(u16 port, struct str_buf *buf);

#endif // __TX_COM__
