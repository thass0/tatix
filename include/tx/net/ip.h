// Internet protocol (IP) implementation (only version 4).

#ifndef __TX_NET_IP_H__
#define __TX_NET_IP_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/net/ip_addr.h>
#include <tx/net/netdev.h>
#include <tx/net/send_buf.h>

#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_TCP 6

struct ipv4_route_entry {
    struct ipv4_addr dest; // Destination IP address (not necessarily the same network as this host).
    struct ipv4_addr mask; // Mask to compare a given destination address to the `dest` field.
    struct ipv4_addr gateway; // IP address of the host on this network to send the datagram to.
    struct ipv4_addr interface; // IP address of the interface (netdev) to send the datagram from.
};

struct result ipv4_handle_packet(struct input_packet *pkt, struct send_buf sb, struct arena tmp);

// `proto` is one of the `IPV4_PROTOCL_*` constants.
struct result ipv4_send_packet(struct ipv4_addr dest_ip, u8 proto, struct send_buf sb, struct arena arn);

net_u16 internet_checksum(struct byte_view data);
net_u16 internet_checksum_iterate(net_u16 checksum, struct byte_view data);
net_u16 internet_checksum_finalize(net_u16 checksum);

struct result ipv4_route_add(struct ipv4_route_entry ent);

// Return the outward-facing IP address of the interface that is used to reach `dest_ip`. This function performs
// a lookup in the routing table and returns the `interface` field from the entry matching `dest_ip` if one exists.
//
// TCP (and UDP and possibly others) computes an end-to-end checksum that includes fields from the IP header. The TCP
// implementation uses this function to find out what the source IP address field will be in the outgoing datagram.
// It needs to know this to compute the end-to-end checksum.
struct result_ipv4_addr ipv4_route_interface_addr(struct ipv4_addr dest_ip);

// Return the device MTU for the interface that will be used to route outgoing traffic destined for `dest_ip`.
struct result_sz ipv4_route_mtu(struct ipv4_addr dest_ip);

#endif // __TX_NET_IP_H__
