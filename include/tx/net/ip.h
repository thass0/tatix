// Internet protocol (IP) implementation (only version 4).

#ifndef __TX_NET_IP_H__
#define __TX_NET_IP_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/net/ip_addr.h>
#include <tx/net/netdev.h>
#include <tx/net/send_buf.h>

#define IPV4_PROTOCOL_ICMP 1

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

struct result ipv4_route_add(struct ipv4_route_entry ent);

#endif // __TX_NET_IP_H__
