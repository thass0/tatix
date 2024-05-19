#ifndef _MEM_H_
#define _MEM_H_

#include <base.h>

// Move `n` bytes starting at `src` to `dst`. The n-byte regions at
// `src` and `dst` may overlap. The number of bytes moved is returned.
size mem_move(void *dst, void *src, size n);
size mem_move_volatile(volatile void *dst, volatile void *src, size n);

#endif // _MEM_H_
