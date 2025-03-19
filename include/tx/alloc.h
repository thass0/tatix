#ifndef __TX_ALLOC_H__
#define __TX_ALLOC_H__

#include <tx/assert.h>
#include <tx/base.h>

typedef void *(*alloc_func_t)(void *a, sz size, sz align);
typedef void (*free_func_t)(void *a, void *ptr, sz size);

struct alloc {
    void *a_ptr; // Allocator structure
    alloc_func_t alloc;
    free_func_t free;
};

static inline struct alloc alloc_new(void *a_ptr, alloc_func_t alloc, free_func_t free)
{
    struct alloc a;

    assert(a_ptr);
    assert(alloc);
    assert(free);

    a.a_ptr = a_ptr;
    a.alloc = alloc;
    a.free = free;
    return a;
}

static inline void *alloc_alloc(struct alloc a, sz size, sz align)
{
    return a.alloc(a.a_ptr, size, align);
}

static inline void alloc_free(struct alloc a, void *ptr, sz size)
{
    a.free(a.a_ptr, ptr, size);
}

#endif // __TX_ALLOC_H__
