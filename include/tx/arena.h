#ifndef __TX_ARENA_H__
#define __TX_ARENA_H__

#include <tx/assert.h>
#include <tx/base.h>
#include <tx/byte.h>

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

// Create a new arena that uses `bb` as its source of memory.
static inline struct arena arena_new(struct byte_array ba)
{
    struct arena arn;

    arn.beg = ba.dat;
    arn.end = arn.beg + ba.len;

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
    byte_array_set(byte_array_new(p, n_bytes), 0);
    return p;
}

// Allocate `n_bytes` out of the arena. Crashes if the arena doesn't have
// enough space. Never returns NULL. The returned bytes are always zeroed.
static inline void *arena_alloc(struct arena *arn, sz n_bytes)
{
    return arena_alloc_aligned(arn, n_bytes, alignof(void *));
}

static inline void *arena_alloc_aligned_array(struct arena *arn, sz n, sz size, sz align)
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
    return arena_alloc_aligned(arn, n * size, align);
}

// Alloactes `n * size` bytes of of the arena. Will return `NULL` if the
// arguments overflow the multiplication.
static inline void *arena_alloc_array(struct arena *arn, sz n, sz size)
{
    return arena_alloc_aligned_array(arn, n, size, alignof(void *));
}

static inline struct str_buf str_buf_from_arena(struct arena *arn, sz cap)
{
    struct str_buf buf;
    buf.dat = arena_alloc_array(arn, cap, sizeof(*buf.dat));
    buf.len = 0;
    buf.cap = cap;
    return buf;
}

static inline struct byte_array byte_array_from_arena(sz n, struct arena *arn)
{
    assert(arn);
    assert(n > 0);

    return byte_array_new(arena_alloc(arn, n), n);
}

#endif // __TX_ARENA_H__
