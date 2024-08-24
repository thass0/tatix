#ifndef __TXPOOL_H__
#define __TX_POOL_H__

#include <tx/assert.h>
#include <tx/base.h>
#include <tx/bytes.h>

struct pool {
    ptr *head; /* First block in the list */
    sz size; /* Size of each block */
};

/**
 * Create a new pool. It uses `bytes.dat` as its source of memory. The minimum
 * block size is `MAX(sizeof(ptr), alignof(void *))`.
 */
static inline struct pool pool_new(struct bytes bytes, sz block_size)
{
    struct pool pool = { .head = NULL, .size = 0 };
    sz align = MAX(sizeof(ptr), alignof(void *));
    ptr *block = NULL;
    sz n_blocks = 0;

    assert(bytes.dat);
    assert(bytes.len > 0);
    assert(block_size > 0);

    assert(align <= SZ_MAX - block_size);
    block_size = (block_size + align - 1) & ~(align - 1);

    assert(block_size > 0);
    n_blocks = bytes.len / block_size;

    /* Link all the blocks that are available */
    for (sz i = 0; i < n_blocks * block_size; i += block_size) {
        block = (ptr *)(bytes.dat + i);
        *block = (ptr)pool.head;
        pool.head = block;
    }

    pool.size = block_size;

    return pool;
}

/**
 * Allocate a block from the pool. Returns `NULL` if the pool is empty.
 */
static inline void *pool_alloc(struct pool *pool)
{
    ptr *block = NULL;

    assert(pool);

    if (!pool->head)
        return NULL;

    block = pool->head;
    pool->head = (ptr *)*block;
    memset(bytes_new(block, pool->size), 0);

    return block;
}

/**
 * Free a block back to the pool.
 */
static inline void pool_free(struct pool *pool, void *block)
{
    assert(pool);
    assert(block);

    *(ptr *)block = (ptr)pool->head;
    pool->head = block;
}

#endif /* __TX_POOL_H__ */
