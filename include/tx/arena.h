#ifndef __TX_ARENA_H__
#define __TX_ARENA_H__

#include <tx/assert.h>
#include <tx/base.h>
#include <tx/bytes.h>

// Based on this eye-opening post: https://nullprogram.com/blog/2023/09/27/

// General note on arena usage: If some function takes a `struct arena *` as an
// arguments, that means the function intends to make an allocation out of that
// arena that will outlive the function. If some function takes a `struct arena`
// (not a pointer this time) as an argument, that means the function intends to
// make a temporary internal allocation.

struct arena {
    byte *beg;
    byte *end;
};

// Create a new arena. It uses `buf` as its source of memory. buf is assumed
// to have a capacity of `buf_cap` bytes.
static inline struct arena arena_new(void *buf, sz buf_cap)
{
    struct arena arn;

    assert(buf);

    arn.beg = buf;
    arn.end = arn.beg + buf_cap;

    return arn;
}

static inline void *arena_alloc_aligned(struct arena *arn, sz n_bytes, sz align)
{
    assert(arn);

    // Code adapted from "Arena allocator tips and tricks" by Chris Wellons,
    // https://nullprogram.com/blog/2023/09/27/

    sz padding = -(uptr)arn->beg & (align - 1);
    sz available = arn->end - arn->beg - padding;
    if (available < 0 || n_bytes > available)
        crash("Out of memory");
    void *p = arn->beg + padding;
    arn->beg += padding + n_bytes;
    memset(bytes_new(p, n_bytes), 0);
    return p;
}

// Allocate `n_bytes` out of the arena. Crashes if the arena doesn't have
// enough space. Never returns NULL. The returned bytes are always zeroed.
static inline void *arena_alloc(struct arena *arn, sz n_bytes)
{
    return arena_alloc_aligned(arn, n_bytes, alignof(void *));
}

// Alloactes `n * size` bytes of of the arena. Will return `NULL` if the
// arguments overflow the multiplication.
static inline void *arena_alloc_array(struct arena *arn, sz n, sz size)
{
    // Overflow check based on "calloc when multiply overflows" by David Jones:
    // https://drj11.wordpress.com/2008/06/04/calloc-when-multiply-overflows/

#if SZ_WIDTH == 32
    if (((n > 65535) || (size > 65535)) && SZ_MAX / n < size)
        return NULL;
#elif SZ_WIDTH == 64
    if (((n > 4294967295) || (size > 4294967295)) && SZ_MAX / n < size)
        return NULL;
#else
#error "Unsupported width of sz"
#endif
    return arena_alloc_aligned(arn, n * size, alignof(void *));
}

static inline struct str_buf fmt_buf_new(struct arena *arn, sz cap)
{
    struct str_buf buf;
    buf.dat = arena_alloc_array(arn, cap, sizeof(*buf.dat));
    buf.len = 0;
    buf.cap = cap;
    return buf;
}

#endif // __TX_ARENA_H__
