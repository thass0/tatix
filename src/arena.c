#include <arena.h>

void *alloc(struct arena *arn, sz size, sz align, sz count)
{
    if (arn == NULL || size <= 0 || align <= 0 || count <= 0)
        return NULL;

    sz padding = -(uptr)arn->beg & (align - 1);
    sz avail = arn->end - arn->beg - padding;

    if (avail < 0 || count > avail / size)
        return NULL;

    void *p = arn->beg + padding;
    arn->beg += padding + count * size;

    u8 *p_cpy = p;
    while (p_cpy < arn->beg)
        *(p_cpy++) = 0;

    return p;
}
