#ifndef __TX_ARENA_H__
#define __TX_ARENA_H__

#include <tx/base.h>

// Based on this eye-opening post: https://nullprogram.com/blog/2023/09/27/

// If some function takes a `struct arena *` as an argument, that means it indents
// to make an allocation that will stay alive outside of this function.
// If some function takes a `struct arena` (not a pointer this time) as an argument,
// that means it intends to make a temporary, internal allocation.

struct arena {
    u8 *beg;
    u8 *end;
};

#define NEW_ARENA(b, size) \
    (struct arena) { .beg = (b), .end = (b) + (size) }

// Allocate `count` items out of the arena that are `size` bytes each. The
// pointer that's returned on success is aligned to a boundary of `align`
// bytes. On error, `NULL` is returned.
void *alloc(struct arena *arn, sz size, sz align, sz count);

#define NEW(a, t, n) (t *)alloc(a, sizeof(t), __alignof__(t), n)

#endif // __TX_ARENA_H__
