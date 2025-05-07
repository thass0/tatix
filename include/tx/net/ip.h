// Internet protocol (IP) implementation (only version 4).

#ifndef __TX_NET_IP_H__
#define __TX_NET_IP_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/net/ip_addr.h>
#include <tx/net/netdev.h>
#include <tx/net/send_buf.h>

#define IPV4_PROTOCOL_ICMP 1

void ipv4_handle_packet(struct input_packet *pkt, struct send_buf sb, struct arena tmp);

// `proto` is one of the `IPV4_PROTOCL_*` constants.
struct result ipv4_send_packet(struct ipv4_addr dest_ip, u8 proto, struct send_buf sb, struct arena arn);

net_u16 internet_checksum(struct byte_view data);

#endif // __TX_NET_IP_H__
