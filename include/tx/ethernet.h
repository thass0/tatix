// Ethernet frame definitions (as specified by IEEE 802.3).

#ifndef __TX_ETHERNET_H__
#define __TX_ETHERNET_H__

#include <tx/base.h>
#include <tx/mac.h>
#include <tx/netorder.h>

#define ETHERNET_PTYPE_IPV4 0x0800
#define ETHERNET_PTYPE_ARP 0x0806

#define ETHERNET_MAX_FRAME_SIZE 1522

// This is the data linker layer (layer 2) format. I.e., the one _without_ the preamble, start frame delimiter and
// Interpacket gap.
struct ethernet_frame_header {
    struct mac_addr dest;
    struct mac_addr src;
    net_u16 ether_type;
    u8 payload[];
};

static_assert(sizeof(struct ethernet_frame_header) == 14);

// TODO: This is called by the e1000 in the interrupt handler. It would be better if frames were processed outside
// of the interrupt handler.
void ethernet_handle_frame(struct mac_addr mac_addr, struct byte_view frame);

#endif // __TX_ETHERNET_H__
