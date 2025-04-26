#ifndef __TX_POOL_H__
#define __TX_POOL_H__

#include <tx/arena.h>
#include <tx/assert.h>
#include <tx/base.h>
#include <tx/byte.h>

struct pool {
    ptr *head; /* First block in the list */
    sz size; /* Size of each block */
};

/**
 * Create a new pool. It uses `ba.dat` as its source of memory. The minimum
 * block size is `MAX(sizeof(ptr), alignof(void *))`.
 */
static inline struct pool pool_new(struct byte_array ba, sz block_size)
{
    struct pool pool = { .head = NULL, .size = 0 };
    sz align = MAX(sizeof(ptr), alignof(void *));
    ptr *block = NULL;
    sz n_blocks = 0;

    assert(ba.dat);
    assert(ba.len > 0);
    assert(block_size > 0);

    assert(align <= SZ_MAX - block_size);
    block_size = (block_size + align - 1) & ~(align - 1);

    assert(block_size > 0);
    n_blocks = ba.len / block_size;

    /* Link all the blocks that are available */
    for (sz i = 0; i < n_blocks * block_size; i += block_size) {
        block = (ptr *)(ba.dat + i);
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
    byte_array_set(byte_array_new(block, pool->size), 0);

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

/**
 * Allocate a pool from an arena.
 */
static inline struct pool *pool_from_arena(sz n, sz size, struct arena *arn)
{
    assert(size > 0);
    assert(n > 0);
    assert(arn);
    struct pool *pool = arena_alloc(arn, sizeof(*pool));
    void *buf = arena_alloc_aligned_array(arn, n, size, size);
    assert(buf);
    assert(n <= SZ_MAX / size);
    *pool = pool_new(byte_array_new(buf, n * size), size);
    return pool;
}

#endif /* __TX_POOL_H__ */
