#ifndef _ARENA_H_
#define _ARENA_H_

#include <base.h>

// Based on this eye-opening post: https://nullprogram.com/blog/2023/09/27/

struct arena {
    u8* beg;
    u8* end;
};

// Allocate `count` items out of the arena that are `size` bytes each. The
// pointer that's returned on success is aligned to a boundary of `align`
// bytes. On error, `NULL` is returned.
void* alloc(struct arena* arn, sz size, sz align, sz count);

// Return the number of bytes available.
sz free_space(struct arena* arn);

#define NEW(a, t, n) (t*)alloc(a, sizeof(t), __alignof__(t), n)

#endif // _ARENA_H_
