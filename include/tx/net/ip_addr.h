// IPv4 address definitions (only version 4).

#ifndef __TX_NET_IP_ADDR_H__
#define __TX_NET_IP_ADDR_H__

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

// Apply `mask` to `addr` and return the result.
static inline struct ipv4_addr ipv4_addr_mask(struct ipv4_addr addr, struct ipv4_addr mask)
{
    struct ipv4_addr ret;
    ret.addr[0] = addr.addr[0] & mask.addr[0];
    ret.addr[1] = addr.addr[1] & mask.addr[1];
    ret.addr[2] = addr.addr[2] & mask.addr[2];
    ret.addr[3] = addr.addr[3] & mask.addr[3];
    return ret;
}

// An IPv4 address with its subnet mask as returned by `ipv4_addr_parse`.
struct ipv4_addr_parsed {
    struct ipv4_addr addr;
    struct ipv4_addr mask;
};

struct_result(ipv4_addr_parsed, struct ipv4_addr_parsed);
struct_option(ipv4_addr_parsed, struct ipv4_addr_parsed);

// Parse an IPv4 address. The format of the IPv4 address is `d.d.d.d[/p]` where each `d` is a
// decimal number in the range 0 to 255 (inclusive). The optional prefix length `p` is a decimal
// number in the range 1 to 32 (inclusive).
struct result_ipv4_addr_parsed ipv4_addr_parse(struct str str);

// Run a self-test on the `ipv4_addr_parse` function.
void ipv4_test_addr_parse(struct arena arn);

#endif // __TX_NET_IP_ADDR_H__
