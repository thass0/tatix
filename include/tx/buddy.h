#ifndef __TX_BUDDY_H__
#define __TX_BUDDY_H__

#include <tx/arena.h>
#include <tx/base.h>
#include <tx/bytes.h>
#include <tx/list.h>

#define PAGE_SIZE_SHIFT 12
#define N_FREE_LISTS ((sizeof(void *) * BYTE_WIDTH) - PAGE_SIZE_SHIFT)

struct block {
    struct dlist link;
    sz ord;
};

struct buddy {
    struct block avail[N_FREE_LISTS];
    struct bytes bitmap;
    sz max_ord;
    byte *base;
};

struct buddy *buddy_init(struct bytes area, struct arena *arn);
void *buddy_alloc(struct buddy *buddy, sz ord);
void buddy_free(struct buddy *buddy, void *ptr, sz ord);

#endif // __TX_BUDDY_H__
