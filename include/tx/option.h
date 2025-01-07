#ifndef __TX_OPTION_H__
#define __TX_OPTION_H__

#include <tx/assert.h>
#include <tx/base.h>

// NOTE: `struct_option(name, type)` can only be used in contexts, where `assert` is availble.
// This is essentially everywhere, except in the functions used to implement `assert`.

#define struct_option(name, type)                                                          \
    struct option_##name {                                                                 \
        bool is_none;                                                                      \
        type unchecked_option_value;                                                       \
    };                                                                                     \
                                                                                           \
    static inline type option_##name##_checked(struct option_##name o)                     \
    {                                                                                      \
        assert(!o.is_none);                                                                \
        return o.unchecked_option_value;                                                   \
    }                                                                                      \
                                                                                           \
    static inline struct option_##name option_##name##_none(void)                          \
    {                                                                                      \
        struct option_##name o;                                                            \
        memset(bytes_new(&o.unchecked_option_value, sizeof(o.unchecked_option_value)), 0); \
        o.is_none = true;                                                                  \
        return o;                                                                          \
    }                                                                                      \
                                                                                           \
    static inline struct option_##name option_##name##_ok(type t)                          \
    {                                                                                      \
        struct option_##name o;                                                            \
        o.is_none = false;                                                                 \
        o.unchecked_option_value = t;                                                      \
        return o;                                                                          \
    }

struct_option(sz, sz)

#endif // __TX_OPTION_H__
