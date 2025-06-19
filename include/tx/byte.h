// View (read only), array (read/write), and buffer (read/write/append) definitions that operate on raw bytes in memory.

#ifndef __TX_BYTE_H__
#define __TX_BYTE_H__

#include <tx/assert.h>
#include <tx/base.h>
#include <tx/stringdef.h>

// Here is a diagram that summarizes how you can convert between the structures in this file:
//
//     READ ONLY        +----> byte_view <----+         <--- str
//                      |                     |
//                ------------------------------------
//                      |                     |
//     READ/WRITE   byte_array <-------> byte_buffer    <--- str_buf

// A byte view is a read-only slice of raw bytes in memory. Writing data to a byte view is in all cases
// forbidden, even if not always catastrophic. It's for your own benefit, dear programmer, to adhere
// to this rule.
struct byte_view {
    byte *dat;
    sz len;
};

// A byte array is a read/write slice of raw bytes in memory. You can read from it and write to it as you please,
// but, notably, it doesn't have append semantics like a byte buffer does. A read-only byte view can be constructed
// from it at any time, but not the other way around. Use a byte buffer if append semantics are needed.
struct byte_array {
    byte *dat;
    sz len;
};

// A byte buffer is a read/write buffer of raw bytes in memory. You can read from it and write to it as you please.
// The append semantics of a buffer are often the right choice when you want to write byte to memory. A read-only
// byte view can be constructed from a buffer at any time, but not the other way around. Use a byte array if append
// semantics aren't needed.
struct byte_buf {
    byte *dat;
    sz len;
    sz cap;
};

// NOTE: If you are missing any functions that operate on byte buffers, first check if these functions are available
// on byte views before adding them. In case they are available for byte views, just create byte view of the current
// buffer and call the byte view functions.

///////////////////////////////////////////////////////////////////////////////
// Creation and conversions                                                  //
///////////////////////////////////////////////////////////////////////////////

static inline struct byte_view byte_view_new(void *dat, sz len)
{
    struct byte_view bv;
    bv.dat = dat;
    bv.len = len;
    return bv;
}

static inline struct byte_array byte_array_new(void *dat, sz len)
{
    struct byte_array ba;
    ba.dat = dat;
    ba.len = len;
    return ba;
}

static inline struct byte_buf byte_buf_new(void *dat, sz len, sz cap)
{
    struct byte_buf bb;
    bb.dat = dat;
    bb.len = len;
    bb.cap = cap;
    return bb;
}

// Byte buffer conversions.

static inline struct byte_buf byte_buf_from_array(struct byte_array ba)
{
    return byte_buf_new(ba.dat, 0, ba.len);
}

static inline struct byte_buf byte_buf_from_str_buf(struct str_buf sb)
{
    return byte_buf_new(sb.dat, sb.len, sb.cap);
}

static inline void *byte_buf_ptr(struct byte_buf bb)
{
    return (void *)bb.dat;
}

// Byte array conversions.

static inline struct byte_array byte_array_from_buf(struct byte_buf bb)
{
    return byte_array_new(bb.dat, bb.len);
}

// This is a convenience function that returns a void pointer to the start of the byte array. It's nice when you
// want to cast the byte array to some other kind of pointer because it spares you from doing an explicit cast.
static inline void *byte_array_ptr(struct byte_array ba)
{
    return (void *)ba.dat;
}

// Byte view conversions.

static inline struct byte_view byte_view_from_array(struct byte_array ba)
{
    return byte_view_new(ba.dat, ba.len);
}

static inline struct byte_view byte_view_from_buf(struct byte_buf bb)
{
    return byte_view_new(bb.dat, bb.len);
}

static inline struct byte_view byte_view_from_str(struct str sv)
{
    return byte_view_new(sv.dat, sv.len);
}

// Same kind of convenience function as for the byte array to allow setting pointers of different types to the byte
// view's base address without explicit casting.
static inline void *byte_view_ptr(struct byte_view bv)
{
    return (void *)bv.dat;
}

///////////////////////////////////////////////////////////////////////////////
// Operations                                                                //
///////////////////////////////////////////////////////////////////////////////

// NOTE: Functions that operate on byte views live here. These might be things like index-wise compare. But nothing
// will be implemented until a part of the systems needs it.

// Return a new byte view that contains the last `bv.len - n` bytes of `bv`.
static inline struct byte_view byte_view_skip(struct byte_view bv, sz n)
{
    assert(n <= bv.len);
    return byte_view_new(bv.dat + n, bv.len - n);
}

// Use this if you were looking for `memcpy`.
static inline sz byte_buf_append(struct byte_buf *bb, struct byte_view bv)
{
    assert(bb);

    sz n = MIN(bb->cap - bb->len, bv.len);

    for (sz i = 0; i < n; i++)
        bb->dat[bb->len + i] = bv.dat[i];

    bb->len += n;
    return n;
}

static inline sz byte_buf_append_n(struct byte_buf *bb, sz n, byte value)
{
    assert(bb);

    n = MIN(bb->cap - bb->len, n);

    for (sz i = 0; i < n; i++)
        bb->dat[bb->len + i] = value;

    bb->len += n;
    return n;
}

// Use this if you were looking for `memset`.
static inline void byte_array_set(struct byte_array ba, byte value)
{
    for (sz i = 0; i < ba.len; i++)
        ba.dat[i] = value;
}

static inline bool byte_view_is_equal(struct byte_view bv1, struct byte_view bv2)
{
    if (bv1.len != bv2.len)
        return false;
    for (sz i = 0; i < bv1.len; i++)
        if (bv1.dat[i] != bv2.dat[i])
            return false;
    return true;
}

#endif // __TX_BYTE_H__
