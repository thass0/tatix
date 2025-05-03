// Address Resolution Protocol (ARP) implementation for IPv4v4 address resolution.

#ifndef __TX_ARP_H__
#define __TX_ARP_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/ip.h>
#include <tx/mac.h>
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

// Broadcast an ARP REQUEST packet from the device with MAC address `src_mac`. The `src_ip` IPv4 address is the IPv4
// address of this computer (the host). The `dest_ip` is the IPv4 address that we want to known the MAC address for.
// `arn` is used for temporary storage.
//
// NOTE: Consider doing an ARP scan (`netdev_arp_scan`) instead of calling this function directly. With
// `netdev_arp_scan`,  you don't have to care about the `src_mac` MAC address.
struct result arp_send_request(struct ipv4_addr src_ip, struct mac_addr src_mac, struct ipv4_addr dest_ip,
                               struct arena arn);

// Lookup a MAC address associated with the given IPv4 address in the ARP table. Returns either the MAC address or
// nothing.
struct option_mac_addr arp_lookup_mac_addr(struct ipv4_addr ip_addr);

// Handle an ARP packet. Call this function on any incoming packets that were identified as ARP packets. It will
// update the ARP table and reply to the sender (if a reply was requested).
//
// NOTE: This function WILL NOT check if the destination MAC address in the ARP packet belongs to this host. The
// caller of this function which receives the packet should ensure that it's correctly destined for this host.
void arp_handle_packet(struct byte_view packet, struct arena arn);

#endif // __TX_ARP_H__
