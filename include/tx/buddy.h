#ifndef __TX_BUDDY_H__
#define __TX_BUDDY_H__

#include <tx/alloc.h>
#include <tx/arena.h>
#include <tx/base.h>
#include <tx/byte.h>
#include <tx/list.h>
#include <tx/option.h>

#define PAGE_SIZE_SHIFT 12
#define N_FREE_LISTS ((sizeof(void *) * BYTE_WIDTH) - PAGE_SIZE_SHIFT)

struct block {
    struct dlist link;
    sz ord;
};

struct buddy {
    struct block avail[N_FREE_LISTS];
    struct byte_array bitmap;
    sz max_ord;
    byte *base;
};

// Initialize a buddy allocator. `arn` is used to allocate the buddy allocators
// structures so the return value will be a pointer from `arn`.
struct buddy *buddy_init(struct byte_array ba, struct arena *arn);

// Allocate `size` bytes from the given buddy allocator. `buddy` must be non-NULL
// and `size` must be greater than zero. The byte array that this function returns will
// be aligned to a page boundary.
struct option_byte_array buddy_alloc(struct buddy *buddy, sz size);

// Free an allocation from the given buddy allocator. `buddy` must be non-NULL,
// and `ba.len` must match the size must of the original allocation.
void buddy_free(struct buddy *buddy, struct byte_array ba);

// For `struct alloc`.
void *buddy_alloc_wrapper(void *a, sz size, sz align __unused);
void buddy_free_wrapper(void *a, void *ptr, sz size);

#endif // __TX_BUDDY_H__
