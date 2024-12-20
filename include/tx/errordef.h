// This file defines a basic error type without a value in it. See error.h for the version that
// can contain a value on success. The reason for the split is that assert is used by the result
// with values, but the implementation of assert uses `struct result` and we want to avoid
// a recursive dependency.

#ifndef __TX_ERRORDEF_H__
#define __TX_ERRORDEF_H__

#include <tx/base.h>
#include <tx/bytes.h>

// Many languages now feature results and options as ways of returning a value
// that indicates either a success and a valid return value, or an error along
// with an error code. Implementing this properly requires sum types, which C
// doesn't support. C also doesn't have type-level polymorphism, so we can't
// create structures that are generic over the type of some field (e.g. the
// types of the success and error values could be generics).
//
// Two patterns are commonly used in C programs:
// 1. The `int` return value that's negative on error and contains, e.g., and `errno`.
// 2. An error pointer that also contains either an error or a pointer
//
// The downsides of each of these is that at the type-level, it's not visible
// that some values are potential errors while other aren't. It is also easy
// to ignore possible error values or forget checks. Additionally, just from
// looking at the function interface, there is no way to know what values are
// errors and what values are valid.
//
// The `struct result` type aims to be a middle-ground. It contains fields indicating
// if there has been an error, an error code, and, possibly, a value that's returned on
// success. To make the structure generic over the type of the return value, a macro
// is provided that defines the struct along with it's default functions for any type.
//
// This is likely less performant than using the aforementioned methods of error handling.
// However, it's conceivable to optimize some common cases like pointers with niching.
// Regardless, clarity and safety should come first, and once both are established and
// code has been written, the performance of this code can be measured and the
// implementation of the result type can be optimized.

// This is the case where return value is provided on success.
struct result {
    bool is_error;
    u16 code;
};

static_assert(sizeof(struct result) == 4);

static inline struct result result_error(u16 code)
{
    struct result r;
    r.is_error = true;
    r.code = code;
    return r;
}

static inline struct result result_ok(void)
{
    struct result r;
    r.is_error = false;
    r.code = 0;
    return r;
}

// Error codes.
// These are the errno values because these are well-known and seem exhaustive enough.
// For example, see https://en.wikipedia.org/wiki/Errno.h

#define EIO 5
#define ENOMEM 12
#define EINVAL 22

#endif // __TX_ERRORDEF_H__
