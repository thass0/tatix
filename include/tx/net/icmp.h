// Internet Control Message Protocol (ICMP) implementation (version 4 only).

#ifndef __TX_NET_ICMP_H__
#define __TX_NET_ICMP_H__

#include <tx/byte.h>
#include <tx/error.h>
#include <tx/net/ip.h>
#include <tx/net/ip_addr.h>
#include <tx/net/send_buf.h>

struct result icmpv4_send_echo(struct ipv4_addr dest_addr, u16 ident, u16 seq, struct send_buf sb, struct arena arn);

struct result icmpv4_handle_message(struct ipv4_addr src_addr, struct byte_view message, struct send_buf sb,
                                    struct arena arn);

#endif // __TX_NET_ICMP_H__
