// Ethernet frame definitions (as specified by IEEE 802.3).

#ifndef __TX_ETHERNET_H__
#define __TX_ETHERNET_H__

#include <tx/base.h>
#include <tx/error.h>
#include <tx/netorder.h>

#define ETHERNET_PTYPE_IPV4 0x0800
#define ETHERNET_PTYPE_ARP 0x0806

struct mac_addr {
    u8 addr[6];
} __packed;

static_assert(sizeof(struct mac_addr) == 6);

struct_result(mac_addr, struct mac_addr);

#define MAC_ADDR_BROADCAST mac_addr(0xff, 0xff, 0xff, 0xff, 0xff, 0xff)

static inline struct mac_addr mac_addr(u8 a1, u8 a2, u8 a3, u8 a4, u8 a5, u8 a6)
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

// This is the data linker layer (layer 2) format. I.e., the one _without_ the preamble, start frame delimiter and
// Interpacket gap.
struct ethernet_frame_header {
    struct mac_addr dest;
    struct mac_addr src;
    net_u16 ether_type;
    u8 payload[];
};

static_assert(sizeof(struct ethernet_frame_header) == 14);

#endif // __TX_ETHERNET_H__
