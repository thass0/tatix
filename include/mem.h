#ifndef _MEM_H_
#define _MEM_H_

#include <base.h>
#include <arena.h>

// Move `n` bytes starting at `src` to `dst`. The n-byte regions at
// `src` and `dst` may overlap. The number of bytes moved is returned.
sz memmove(void *dst, void *src, sz n, struct arena *arn);
sz memmove_volatile(volatile void *dst, volatile void *src, sz n, struct arena *arn);

#endif // _MEM_H_
