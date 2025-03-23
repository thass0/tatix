#ifndef __TX_KVALLOC_H__
#define __TX_KVALLOC_H__

#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/bytes.h>
#include <tx/error.h>
#include <tx/stringdef.h>

// Initialize kvalloc. `vaddrs` is the range of virtual addresses that kvalloc will
// manage. All addresses in this range must be accessible.
struct result kvalloc_init(struct bytes vaddrs);

// Allocate `n_bytes` bytes with an alignment of at least `align` bytes.
// kvalloc must be initialized before calling this function for the first time.
void *kvalloc_alloc(sz n_bytes, sz align);

// Deallocate `n_bytes` bytes starting at address `ptr`.
void kvalloc_free(void *ptr, sz n_bytes);

// Wrappers for tx/alloc.h. `a` isn't used.
void *kvalloc_alloc_wrapper(void *a, sz size, sz align);
void kvalloc_free_wrapper(void *a, void *ptr, sz size);

static inline struct str_buf str_buf_from_kvalloc(sz cap)
{
    assert(cap >= 0);
    void *dat = kvalloc_alloc(cap, alignof(char));
    assert(dat);
    return str_buf_new(dat, 0, cap);
}

#endif // __TX_KVALLOC_H__
