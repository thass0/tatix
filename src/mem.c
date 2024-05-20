#include <mem.h>
#include <arena.h>

#define MEMMOVE_IMPL(name, dst_type, src_type)                          \
    sz name(dst_type dst, src_type src, sz n, struct arena scratch)     \
    {                                                                   \
        if (dst == NULL || src == NULL || n <= 0)                       \
            return 0;                                                   \
                                                                        \
        sz avail = free_space(&scratch);                                \
        n = n > avail ? avail : n;                                      \
        u8 *buf = NEW(&scratch, u8, n);                                 \
        if (buf == NULL)                                                \
            return 0;                                                   \
                                                                        \
        for (sz i = 0; i < n; i++)                                      \
            buf[i] = ((u8*)src)[i];                                     \
        for (sz i = 0; i < n; i++)                                      \
            ((u8*)dst)[i] = buf[i];                                     \
                                                                        \
        return n;                                                       \
    }

MEMMOVE_IMPL(memmove, void *, void *);
MEMMOVE_IMPL(memmove_volatile, volatile void *, volatile void *);
