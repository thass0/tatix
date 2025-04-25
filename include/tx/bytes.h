#ifndef __TX_BYTES_H__
#define __TX_BYTES_H__

#include <tx/base.h>
#include <tx/stringdef.h>

struct bytes {
    byte *dat;
    sz len;
};

struct bytes_buf {
    byte *dat;
    sz len;
    sz cap;
};

static inline struct bytes_buf bytes_buf_new(void *dat, sz len, sz cap)
{
    struct bytes_buf buf;
    buf.dat = dat;
    buf.len = len;
    buf.cap = cap;
    return buf;
}

static inline struct bytes bytes_new(void *dat, sz len)
{
    struct bytes bytes;
    bytes.dat = dat;
    bytes.len = len;
    return bytes;
}

static inline struct bytes bytes_from_buf(struct bytes_buf buf)
{
    struct bytes bytes;
    bytes.dat = buf.dat;
    bytes.len = buf.len;
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
    sz len = MIN(dest.len, src.len);
    for (sz i = 0; i < len; i++)
        dest.dat[i] = src.dat[i];
}

static inline void bytes_buf_memcpy(struct bytes_buf *dest, struct bytes src)
{
    sz len = MIN(dest->cap - dest->len, src.len);
    for (sz i = 0; i < len; i++)
        dest->dat[i] = src.dat[i];
    dest->len += len;
}

#endif // __TX_BYTES_H__
