// Address Resolution Protocol (ARP) implementation for IPv4v4 address resolution.

#ifndef __TX_ARP_H__
#define __TX_ARP_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/ip.h>
#include <tx/mac.h>
#include <tx/netdev.h>
#include <tx/netorder.h>

#define ARP_OPCODE_REQUEST 1
#define ARP_OPCODE_REPLY 2

#define ARP_HTYPE_ETHERNET 1

struct arp_header {
    net_u16 htype;
    net_u16 ptype;
    u8 hlen;
    u8 plen;
    net_u16 opcode;
    u8 payload[];
} __packed;

static_assert(sizeof(struct arp_header) == 8);

// Broadcast an ARP REQUEST packet from the device `netdev`. The `dest_ip` is the IPv4 address that we want to known
// the MAC address for.
struct result arp_send_request(struct ipv4_addr dest_ip, struct netdev *netdev, struct send_buf sb, struct arena tmp);

// Lookup a MAC address associated with the given IPv4 address in the ARP table. Returns either the MAC address or
// nothing.
struct option_mac_addr arp_lookup_mac_addr(struct ipv4_addr ip_addr);

// Handle an ARP packet. Call this function on any incoming packets that were identified as ARP packets. It will
// update the ARP table and reply to the sender using the same device that the ARP packet was received on.
//
// NOTE: This function WILL NOT check if the destination MAC address in the ARP packet belongs to this host. The
// caller of this function which receives the packet should ensure that it's correctly destined for this host.
void arp_handle_packet(struct input_packet *pkt, struct send_buf sb, struct arena tmp);

#endif // __TX_ARP_H__
