#ifndef __TX_ERROR_H__
#define __TX_ERROR_H__

#include <tx/assert.h>
#include <tx/base.h>
#include <tx/byte.h>
#include <tx/errordef.h>

// NOTE: `struct_result(name, type)` can only be used in contexts, where `assert` is availble.
// This is essentially everywhere, except in the functions used to implement `assert`.

#define struct_result(name, type)                                                                       \
    struct result_##name {                                                                              \
        bool is_error;                                                                                  \
        u16 code;                                                                                       \
        type unchecked_result_value;                                                                    \
    };                                                                                                  \
                                                                                                        \
    static inline type result_##name##_checked(struct result_##name r)                                  \
    {                                                                                                   \
        assert(!r.is_error);                                                                            \
        return r.unchecked_result_value;                                                                \
    }                                                                                                   \
                                                                                                        \
    static inline struct result_##name result_##name##_error(u16 code)                                  \
    {                                                                                                   \
        struct result_##name r;                                                                         \
        byte_array_set(byte_array_new(&r.unchecked_result_value, sizeof(r.unchecked_result_value)), 0); \
        r.is_error = true;                                                                              \
        r.code = code;                                                                                  \
        return r;                                                                                       \
    }                                                                                                   \
                                                                                                        \
    static inline struct result_##name result_##name##_ok(type t)                                       \
    {                                                                                                   \
        struct result_##name r;                                                                         \
        r.is_error = false;                                                                             \
        r.code = 0;                                                                                     \
        r.unchecked_result_value = t;                                                                   \
        return r;                                                                                       \
    }                                                                                                   \
                                                                                                        \
    typedef char REQUIRE_SEMICOLON_AFTER_MACRO_STRUCT_RESULT_##name

struct_result(sz, sz);
struct_result(ptr, ptr);
struct_result(u32, u32);
struct_result(u16, u16);
struct_result(u8, u8);
struct_result(byte, byte);
struct_result(bool, bool);

#endif // __TX_ERROR_H__
