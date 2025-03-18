#ifndef __TX_KVALLOC_H__
#define __TX_KVALLOC_H__

#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/bytes.h>
#include <tx/error.h>

// Initialize kvalloc. `vaddrs` is the range of virtual addresses that kvalloc will
// manage. All addresses in this range must be accessible.
struct result kvalloc_init(struct bytes vaddrs);

// Allocate `n_bytes` bytes with an alignment of at least `align` bytes.
// kvalloc must be initialized before calling this function for the first time.
void *kvalloc_alloc(sz n_bytes, sz align);

// Deallocate `n_bytes` bytes starting at address `ptr`.
void kvalloc_free(void *ptr, sz n_bytes);

#endif // __TX_KVALLOC_H__
