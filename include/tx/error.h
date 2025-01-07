#ifndef __TX_ERROR_H__
#define __TX_ERROR_H__

#include <tx/assert.h>
#include <tx/base.h>
#include <tx/errordef.h>

// NOTE: `struct_result(name, type)` can only be used in contexts, where `assert` is availble.
// This is essentially everywhere, except in the functions used to implement `assert`.

#define struct_result(name, type)                                                          \
    struct result_##name {                                                                 \
        bool is_error;                                                                     \
        u16 code;                                                                          \
        type unchecked_result_value;                                                       \
    };                                                                                     \
                                                                                           \
    static inline type result_##name##_checked(struct result_##name r)                     \
    {                                                                                      \
        assert(!r.is_error);                                                               \
        return r.unchecked_result_value;                                                   \
    }                                                                                      \
                                                                                           \
    static inline struct result_##name result_##name##_error(u16 code)                     \
    {                                                                                      \
        struct result_##name r;                                                            \
        memset(bytes_new(&r.unchecked_result_value, sizeof(r.unchecked_result_value)), 0); \
        r.is_error = true;                                                                 \
        r.code = code;                                                                     \
        return r;                                                                          \
    }                                                                                      \
                                                                                           \
    static inline struct result_##name result_##name##_ok(type t)                          \
    {                                                                                      \
        struct result_##name r;                                                            \
        r.is_error = false;                                                                \
        r.code = 0;                                                                        \
        r.unchecked_result_value = t;                                                      \
        return r;                                                                          \
    }                                                                                      \
                                                                                           \
    static inline struct result_##name result_##name##_from_error(struct result res)       \
    {                                                                                      \
        assert(res.is_error);                                                              \
        return result_##name##_error(res.code);                                            \
    }

struct_result(sz, sz)

#endif // __TX_ERROR_H__
