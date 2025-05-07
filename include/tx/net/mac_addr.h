// MAC address definitions.

#ifndef __TX_MAC_ADDR_H__
#define __TX_MAC_ADDR_H__

#include <tx/arena.h>
#include <tx/base.h>
#include <tx/error.h>
#include <tx/fmt.h>
#include <tx/option.h>

struct mac_addr {
    u8 addr[6];
} __packed;

static_assert(sizeof(struct mac_addr) == 6);

struct_result(mac_addr, struct mac_addr);
struct_option(mac_addr, struct mac_addr);

#define MAC_ADDR_BROADCAST mac_addr_new(0xff, 0xff, 0xff, 0xff, 0xff, 0xff)

// Create a new MAC address from the given bytes. The first argument is the first byte in the MAC address. This means
// that `mac_addr_new(0x02, 0x9c, 0x60, 0xae, 0xda, 0x5e)` represents the MAC address 02:9c:60:ae:da:5e.
static inline struct mac_addr mac_addr_new(u8 a1, u8 a2, u8 a3, u8 a4, u8 a5, u8 a6)
{
    struct mac_addr addr;
    addr.addr[0] = a1;
    addr.addr[1] = a2;
    addr.addr[2] = a3;
    addr.addr[3] = a4;
    addr.addr[4] = a5;
    addr.addr[5] = a6;
    return addr;
}

// Compare two MAC addresses and return `true` if they are the same.
static inline bool mac_addr_is_equal(struct mac_addr a1, struct mac_addr a2)
{
    return a1.addr[0] == a2.addr[0] && a1.addr[1] == a2.addr[1] && a1.addr[2] == a2.addr[2] &&
           a1.addr[3] == a2.addr[3] && a1.addr[4] == a2.addr[4] && a1.addr[5] == a2.addr[5];
}

#define MAC_ADDR_FMT_BUF_SIZE 32

// Create a formatted string representation of the given MAC address using memory from the arena allocator `arn`.
static inline struct str mac_addr_format(struct mac_addr addr, struct arena *arn)
{
    struct str_buf sbuf = str_buf_from_byte_array(byte_array_from_arena(MAC_ADDR_FMT_BUF_SIZE, arn));
    assert(!fmt(&sbuf, STR("%hhx:%hhx:%hhx:%hhx:%hhx:%hhx"), addr.addr[0], addr.addr[1], addr.addr[2], addr.addr[3],
                addr.addr[4], addr.addr[5])
                .is_error);
    return str_from_buf(sbuf);
}

#endif // __TX_MAC_ADDR_H__
