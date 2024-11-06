#ifndef __TX_BYTES_H__
#define __TX_BYTES_H__

#include <tx/base.h>
#include <tx/string.h>

struct bytes {
    byte *dat;
    sz len;
};

static inline struct bytes bytes_new(void *dat, sz len)
{
    struct bytes bytes;
    bytes.dat = dat;
    bytes.len = len;
    return bytes;
}

static inline struct bytes bytes_from_str(struct str str)
{
    struct bytes bytes;
    bytes.dat = (byte *)str.dat;
    bytes.len = str.len;
    return bytes;
}

static inline void memset(struct bytes bytes, byte value)
{
    for (sz i = 0; i < bytes.len; i++)
        bytes.dat[i] = value;
}

static inline void memcpy(struct bytes dest, struct bytes src)
{
    sz len = (dest.len < src.len) ? dest.len : src.len;
    for (sz i = 0; i < len; i++)
        dest.dat[i] = src.dat[i];
}

#endif // __TX_BYTES_H__
