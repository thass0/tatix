// IPv4 address definitions (only version 4).

#ifndef __TX_IP_ADDR_H__
#define __TX_IP_ADDR_H__

#include <tx/arena.h>
#include <tx/base.h>
#include <tx/error.h>
#include <tx/fmt.h>
#include <tx/net/netorder.h>
#include <tx/string.h>

struct ipv4_addr {
    u8 addr[4];
} __packed;

static_assert(sizeof(struct ipv4_addr) == 4);

// Create a new IPv4 address from the given bytes. The first argument is the first byte in the IPv4 address. This means
// that `ipv4_addr_new(192, 168, 100, 1)` represents the IPv4 address 192.168.100.1.
static inline struct ipv4_addr ipv4_addr_new(u8 a0, u8 a1, u8 a2, u8 a3)
{
    struct ipv4_addr addr;
    addr.addr[0] = a0;
    addr.addr[1] = a1;
    addr.addr[2] = a2;
    addr.addr[3] = a3;
    return addr;
}

// Compare two IPv4 addresses and return `true` if they are the same.
static inline bool ipv4_addr_is_equal(struct ipv4_addr addr1, struct ipv4_addr addr2)
{
    return addr1.addr[0] == addr2.addr[0] && addr1.addr[1] == addr2.addr[1] && addr1.addr[2] == addr2.addr[2] &&
           addr1.addr[3] == addr2.addr[3];
}

#define IP_ADDR_FMT_BUF_SIZE 32

// Create a formatted string representation of the given IPv4 address using memory from the arena allocator `arn`.
static inline struct str ipv4_addr_format(struct ipv4_addr addr, struct arena *arn)
{
    struct str_buf sbuf = str_buf_from_byte_array(byte_array_from_arena(IP_ADDR_FMT_BUF_SIZE, arn));
    assert(!fmt(&sbuf, STR("%hhu.%hhu.%hhu.%hhu"), addr.addr[0], addr.addr[1], addr.addr[2], addr.addr[3]).is_error);
    return str_from_buf(sbuf);
}

#endif // __TX_IP_ADDR_H__
