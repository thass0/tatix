// Definitions representing network byte order (big-endian) data.

#ifndef __TX_NET_NETORDER_H__
#define __TX_NET_NETORDER_H__

#include <tx/base.h>

// For reference, consider a number like 0x0a0b0c0d. If it's stored in memory starting at address a, we will get:
//
// Little endian:
// a[0] = 0x0d, a[1] = 0x0c, a[2] = 0x0b, a[3] = 0x0a
//
// Bit endian:
// a[0] = 0x0a, a[1] = 0x0b, a[2] = 0x0c, a[3] = 0x0d
//
// I.e., little endian byte order stores the least significant byte at the smallest address, whereas big
// endian byte order stores the most significant bytes at tehe smallest address.

// These are the same if one is byte-swapped, just for fun.
#define ORDER_LITTLE_ENDIAN 0xaabb
#define ORDER_BIG_ENDIAN 0xbbaa

#define NET_BYTE_ORDER ORDER_BIG_ENDIAN

#ifdef __x86_64__
#define SYSTEM_BYTE_ORDER ORDER_LITTLE_ENDIAN
#else
#error System byte order not defined
#endif

typedef struct net_u16 {
    u16 inner;
} __packed net_u16;
typedef struct net_u32 {
    u32 inner;
} __packed net_u32;
typedef struct net_u64 {
    u64 inner;
} __packed net_u64;

#if NET_BYTE_ORDER != SYSTEM_BYTE_ORDER
static inline u16 u16_from_net_u16(net_u16 value)
{
    return __builtin_bswap16(value.inner);
}

static inline u32 u32_from_net_u32(net_u32 value)
{
    return __builtin_bswap32(value.inner);
}

static inline u64 u64_from_net_u64(net_u64 value)
{
    return __builtin_bswap64(value.inner);
}

static inline net_u16 net_u16_from_u16(u16 value)
{
    struct net_u16 n_value;
    n_value.inner = __builtin_bswap16(value);
    return n_value;
}

static inline net_u32 net_u32_from_u32(u32 value)
{
    struct net_u32 n_value;
    n_value.inner = __builtin_bswap32(value);
    return n_value;
}

static inline net_u64 net_u64_from_u64(u64 value)
{
    struct net_u64 n_value;
    n_value.inner = __builtin_bswap64(value);
    return n_value;
}
#else // NET_BYTE_ORDER == SYSTEM_BYTE_ORDER
static inline u16 u16_from_net_u16(net_u16 value)
{
    return value.inner;
}

static inline u32 u32_from_net_u32(net_u32 value)
{
    return value.inner;
}

static inline u64 u64_from_net_u64(net_u64 value)
{
    return value.inner;
}

static inline net_u16 net_u16_from_u16(u16 value)
{
    struct net_u16 n_value;
    n_value.inner = value;
    return n_value;
}

static inline net_u32 net_u32_from_u32(u32 value)
{
    struct net_u32 n_value;
    n_value.inner = value;
    return n_value;
}

static inline net_u64 net_u64_from_u64(u64 value)
{
    struct net_u64 n_value;
    n_value.inner = value;
    return n_value;
}
#endif

#endif // __TX_NET_NETORDER_H__
