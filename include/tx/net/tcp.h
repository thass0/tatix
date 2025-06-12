// Transmission Control Protocol (TCP) implementation.

#ifndef __TX_NET_TCP_H__
#define __TX_NET_TCP_H__

#include <tx/base.h>
#include <tx/net/ip_addr.h>
#include <tx/net/netorder.h>
#include <tx/net/send_buf.h>

///////////////////////////////////////////////////////////////////////////////
// IP side                                                                   //
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// User interface                                                            //
///////////////////////////////////////////////////////////////////////////////

// These functions are meant for using TCP. You are handed internal data here so just don't touch it. We're all
// adults here so this shouldn't be a problem, no?

struct tcp_conn; // Ha, you can't even see what's inside!

// Listen for incoming connections on addr:port and accept the first client that tries to connect.
struct tcp_conn *tcp_conn_listen_accept(struct ipv4_addr addr, u16 port, struct arena tmp);

// Send `payload` to the other side of the connection `conn`. The return value indicates the number of bytes we were
// able to transmit.
struct result_sz tcp_conn_send(struct tcp_conn *conn, struct byte_view payload, struct send_buf sb, struct arena tmp);

// Store data received on the connection `conn` into `buf`. On success, returns the maximum number of bytes available
// to recive. This means 0 is returned if there is no data. In this case, wait a bit and try again. If there is more
// data available than the buffer can fit, the total number of bytes available is returned and the buffer is filled
// to its limit.
struct result_sz tcp_conn_recv(struct tcp_conn *conn, struct byte_buf *buf);

// Close the connection `*conn`. `conn` will be set to NULL since it's stale now.
struct result tcp_conn_close(struct tcp_conn **conn, struct send_buf sb, struct arena tmp);

#endif // __TX_NET_TCP_H__
