#ifndef __TX_KVALLOC_H__
#define __TX_KVALLOC_H__

#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/stringdef.h>

// Initialize kvalloc. `vaddrs` is the range of virtual addresses that kvalloc will
// manage. All addresses in this range must be accessible.
struct result kvalloc_init(struct byte_array vaddrs);

// Allocate `n_bytes` bytes with an alignment of at least `align` bytes.
// kvalloc must be initialized before calling this function for the first time.
struct option_byte_array kvalloc_alloc(sz n_bytes, sz align);

// Deallocate the memory in the `ba`.
void kvalloc_free(struct byte_array ba);

// Wrappers for tx/alloc.h. `a` isn't used.
void *kvalloc_alloc_wrapper(void *a, sz size, sz align);
void kvalloc_free_wrapper(void *a, void *ptr, sz size);

#endif // __TX_KVALLOC_H__
