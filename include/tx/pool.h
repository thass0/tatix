#ifndef __TX_POOL_H__
#define __TX_POOL_H__

#include <tx/arena.h>
#include <tx/assert.h>
#include <tx/base.h>
#include <tx/byte.h>

struct pool {
    struct byte_array mem;
    ptr *head;
    sz size;
};

// Create a new pool allocator that uses the byte array `mem` as its source of memory.
static inline struct pool pool_new(struct byte_array mem, sz block_size)
{
    struct pool pool;
    pool.head = NULL;
    pool.size = 0;

    sz align = MAX(sizeof(ptr), alignof(void *));
    ptr *block = NULL;
    sz n_blocks = 0;

    assert(mem.dat);
    assert(mem.len > 0);
    assert(block_size > 0);

    assert(align <= SZ_MAX - block_size);
    block_size = (block_size + align - 1) & ~(align - 1);

    assert(block_size > 0);
    n_blocks = mem.len / block_size;

    for (sz i = 0; i < n_blocks * block_size; i += block_size) {
        block = (ptr *)(mem.dat + i);
        *block = (ptr)pool.head;
        pool.head = block;
    }

    pool.mem = mem;
    pool.size = block_size;

    return pool;
}

// Allocate a block from the pool. Returns `NULL` if the pool is empty.
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

// Free a block from the pool. Passing `NULL` in the `block` pointer is fine, it will be ignored.
static inline void pool_free(struct pool *pool, void *block)
{
    assert(pool);

    if (!block)
        return;

    assert(IN_RANGE((ptr)block, (ptr)pool->mem.dat, pool->mem.len));

    *(ptr *)block = (ptr)pool->head;
    pool->head = block;
}

#endif // __TX_POOL_H__
