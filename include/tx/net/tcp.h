// Transmission Control Protocol (TCP) implementation.

#ifndef __TX_NET_TCP_H__
#define __TX_NET_TCP_H__

#include <tx/base.h>
#include <tx/net/ip_addr.h>
#include <tx/net/netorder.h>
#include <tx/net/send_buf.h>

struct tcp_ip_pseudo_header {
    struct ipv4_addr src_addr;
    struct ipv4_addr dest_addr;
    u8 zero;
    u8 protocol;
    net_u16 tcp_length;
};

void tcp_init(void);

struct result tcp_handle_packet(struct tcp_ip_pseudo_header pseudo_hdr, struct byte_view segment, struct send_buf sb,
                                struct arena tmp);

#endif // __TX_NET_TCP_H__
