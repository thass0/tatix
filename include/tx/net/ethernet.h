// Ethernet frame definitions (as specified by IEEE 802.3).

#ifndef __TX_ETHERNET_H__
#define __TX_ETHERNET_H__

#include <tx/base.h>
#include <tx/mac.h>
#include <tx/netdev.h>
#include <tx/netorder.h>
#include <tx/option.h>

#define ETHERNET_PTYPE_IPV4 0x0800
#define ETHERNET_PTYPE_ARP 0x0806

#define ETHERNET_MAX_FRAME_SIZE 1522

// This is the data linker layer (layer 2) format. I.e., the one _without_ the preamble, start frame delimiter and
// Interpacket gap.
struct ethernet_frame_header {
    struct mac_addr dest;
    struct mac_addr src;
    net_u16 ether_type;
};

static inline struct option_u16 ethernet_type_from_netdev_proto(netdev_proto_t proto)
{
    switch (proto) {
    case NETDEV_PROTO_IPV4:
        return option_u16_ok(ETHERNET_PTYPE_IPV4);
    case NETDEV_PROTO_ARP:
        return option_u16_ok(ETHERNET_PTYPE_ARP);
    default:
        return option_u16_none();
    }
}

static inline struct option_netdev_proto_t netdev_proto_from_ethernet_type(u16 type)
{
    switch (type) {
    case ETHERNET_PTYPE_IPV4:
        return option_netdev_proto_t_ok(NETDEV_PROTO_IPV4);
    case ETHERNET_PTYPE_ARP:
        return option_netdev_proto_t_ok(NETDEV_PROTO_ARP);
    default:
        return option_netdev_proto_t_none();
    }
}

static_assert(sizeof(struct ethernet_frame_header) == 14);

#endif // __TX_ETHERNET_H__
