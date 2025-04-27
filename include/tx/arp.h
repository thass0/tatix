// Address Resolution Protocol (ARP) implementation for IPv4 address resolution.

#ifndef __TX_ARP_H__
#define __TX_ARP_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/ethernet.h>
#include <tx/ip.h>
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

struct result arp_send_request(struct ip_addr src_ip, struct mac_addr src_mac, struct ip_addr dest_ip,
                               struct arena arn);

struct result_mac_addr arp_lookup_mac_addr(struct ip_addr ip_addr);

void arp_handle_packet(struct byte_view packet);

#endif // __TX_ARP_H__
