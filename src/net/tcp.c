#include <tx/net/tcp.h>

#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/byte.h>
#include <tx/kvalloc.h>
#include <tx/net/ip.h>
#include <tx/net/netorder.h>
#include <tx/print.h>

struct tcp_header {
    net_u16 src_port;
    net_u16 dest_port;
    net_u32 seq_num;
    net_u32 ack_num;
#if SYSTEM_BYTE_ORDER == NET_BYTE_ORDER
    u8 header_len : 4;
    u8 reserved : 4;
#else // SYSTEM_BYTE_ORDER != NET_BYTE_ORDER
    u8 reserved : 4;
    u8 header_len : 4;
#endif // SYSTEM_BYTE_ORDER != NET_BYTE_ORDER
    u8 flags;
    net_u16 window_size;
    net_u16 checksum;
    net_u16 urgent;
};

#define TCP_HEADER_LEN_NO_OPT 5

#define TCP_HDR_FLAG_FIN BIT(0)
#define TCP_HDR_FLAG_SYN BIT(1)
#define TCP_HDR_FLAG_RST BIT(2)
#define TCP_HDR_FLAG_ACK BIT(4)

static_assert(sizeof(struct tcp_header) == 20);

///////////////////////////////////////////////////////////////////////////////
// Manage connections                                                        //
///////////////////////////////////////////////////////////////////////////////

enum tcp_conn_state {
    TCP_CONN_STATE_LISTEN, // Waiting for a client to send a SYN for the connection.
    TCP_CONN_STATE_SYN_RCVD,
    TCP_CONN_STATE_ESTABLISHED,
    TCP_CONN_STATE_CLOSE_WAIT, // We are waiting for the other side to ACK a FIN that we sent it.
    TCP_CONN_STATE_FIN_WAIT_1,
    TCP_CONN_STATE_FIN_WAIT_2,
    TCP_CONN_STATE_CLOSING,
    TCP_CONN_STATE_TIME_WAIT,
};

struct tcp_conn {
    bool is_used;
    struct ipv4_addr host_addr;
    struct ipv4_addr peer_addr;
    u16 host_port;
    u16 peer_port;
    enum tcp_conn_state state;

    // Transmission
    u32 send_unack; // SND.UNA
    u32 send_next; // SND.NXT
    u16 send_window; // SND.WND
    u32 iss; // Initial send sequence number (ISS).

    // Reception
    u32 recv_next; // RCV.NXT
    u16 recv_window; // RCV.WND
};

// If we need to handle more connections at the same time, we could also allocate this array dynamically. The main
// reason for using an array is that it's simple to search (without requiring much pointer chasing like linked lists
// do).
#define TCP_CONN_MAX_NUM 64
static struct tcp_conn global_tcp_conn_table[TCP_CONN_MAX_NUM];

static struct tcp_conn *tcp_alloc_conn(void)
{
    for (sz i = 0; i < TCP_CONN_MAX_NUM; i++) {
        struct tcp_conn *conn = &global_tcp_conn_table[i];
        if (!conn->is_used) {
            conn->is_used = true;
            return conn;
        }
    }

    return NULL;
}

static void tcp_free_conn(struct tcp_conn *conn)
{
    assert(conn);
    // Since we are reusing these, we want to make sure we don't accidentally reuse old data. Thus we set each
    // structure to an easy to recognize bit pattern.
    byte_array_set(byte_array_new((void *)conn, sizeof(*conn)), 0xee);
    conn->is_used = false;
}

static bool ipv4_addr_wildcard_compare(struct ipv4_addr a, struct ipv4_addr b)
{
    struct ipv4_addr zero = ipv4_addr_new(0, 0, 0, 0);
    if (ipv4_addr_is_equal(a, zero) || ipv4_addr_is_equal(b, zero))
        return true;
    return ipv4_addr_is_equal(a, b);
}

static bool port_wildcard_compare(u16 a, u16 b)
{
    if (a == 0 || b == 0)
        return true;
    return a == b;
}

static struct tcp_conn *tcp_lookup_conn(struct ipv4_addr host_addr, struct ipv4_addr peer_addr, u16 host_port,
                                        u16 peer_port)
{
    for (sz i = 0; i < TCP_CONN_MAX_NUM; i++) {
        struct tcp_conn *conn = &global_tcp_conn_table[i];
        if (!conn->is_used)
            continue;

        if (ipv4_addr_wildcard_compare(host_addr, conn->host_addr) &&
            ipv4_addr_wildcard_compare(peer_addr, conn->peer_addr) &&
            port_wildcard_compare(host_port, conn->host_port) && port_wildcard_compare(peer_port, conn->peer_port))
            return conn;
    }

    return NULL;
}

static struct str tcp_fmt_conn(struct ipv4_addr host_addr, struct ipv4_addr peer_addr, u16 host_port, u16 peer_port,
                               struct arena *arn)
{
    assert(arn);
    struct str_buf sbuf = str_buf_from_byte_array(byte_array_from_arena(128, arn));
    assert(!fmt(&sbuf, STR("%s:%hu %s:%hu"), ipv4_addr_format(host_addr, arn), host_port,
                ipv4_addr_format(peer_addr, arn), peer_port)
                .is_error);
    return str_from_buf(sbuf);
}

static bool seq_gt(u32 a, u32 b)
{
    return (i64)a - (i64)b > 0;
}

static inline void tcp_conn_update_unack(struct tcp_conn *conn, u32 ack_num)
{
    assert(conn);

    if (seq_gt(ack_num, conn->send_unack))
        conn->send_unack = ack_num;
}

///////////////////////////////////////////////////////////////////////////////
// Transmit outgoing segments                                                //
///////////////////////////////////////////////////////////////////////////////

static struct result tcp_send_segment_raw(struct ipv4_addr host_addr, struct ipv4_addr peer_addr, u16 host_port,
                                          u16 peer_port, u32 seq_num, u32 ack_num, u16 window_size, u8 flags,
                                          struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    // We need this to compute the checksum over the pseudo header because the pseudo header contains information
    // from the IP layer.
    struct result_ipv4_addr interface_addr_res = ipv4_route_interface_addr(peer_addr);
    if (interface_addr_res.is_error)
        return result_error(interface_addr_res.code);
    struct ipv4_addr interface_addr = result_ipv4_addr_checked(interface_addr_res);

    if (!ipv4_addr_is_equal(interface_addr, host_addr)) {
        // NOTE: The interface address will always match the host address as long as there is only a single interface
        // (as is currently the case). It's possible that the interface address and the host address differ if there
        // are multiple interfaces (e.g., if we receive a segment on one interface and respond on the other), but ---
        // assuming routing tables don't change during a connection --- this will be noticed during the handshake.
        print_dbg(
            PERROR,
            STR("WARNING: IPv4 layer is choosing an interface address (%s) that's different from the host address (%s). Resetting the connection.\n"),
            ipv4_addr_format(interface_addr, &tmp), ipv4_addr_format(host_addr, &tmp));

        flags |= TCP_HDR_FLAG_RST;
    }

    struct tcp_header hdr;
    hdr.src_port = net_u16_from_u16(host_port);
    hdr.dest_port = net_u16_from_u16(peer_port);
    hdr.seq_num = net_u32_from_u32(seq_num);
    hdr.ack_num = net_u32_from_u32(ack_num);
    hdr.header_len = TCP_HEADER_LEN_NO_OPT;
    hdr.reserved = 0;
    hdr.flags = flags;
    hdr.window_size = net_u16_from_u16(window_size);
    hdr.checksum = net_u16_from_u16(0);
    hdr.urgent = net_u16_from_u16(0);

    struct tcp_ip_pseudo_header pseudo_hdr;
    pseudo_hdr.src_addr = interface_addr;
    pseudo_hdr.dest_addr = peer_addr;
    pseudo_hdr.zero = 0;
    pseudo_hdr.protocol = IPV4_PROTOCOL_TCP;
    pseudo_hdr.tcp_length = net_u16_from_u16(sizeof(hdr) + payload.len);

    net_u16 checksum = net_u16_from_u16(0);
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&hdr, sizeof(hdr)));
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&pseudo_hdr, sizeof(pseudo_hdr)));
    checksum = internet_checksum_iterate(checksum, payload);
    hdr.checksum = internet_checksum_finalize(checksum);

    struct byte_buf *buf = NULL;

    if (payload.len > 0) {
        buf = send_buf_prepend(&sb, payload.len);
        if (!buf)
            return result_error(ENOMEM);
        assert(byte_buf_append(buf, payload) == payload.len);
    }

    buf = send_buf_prepend(&sb, sizeof(hdr));
    if (!buf)
        return result_error(ENOMEM);
    assert(byte_buf_append(buf, byte_view_new((void *)&hdr, sizeof(hdr))) == sizeof(hdr));

    return ipv4_send_packet(peer_addr, IPV4_PROTOCOL_TCP, sb, tmp);
}

static struct result_sz tcp_send_segment(struct tcp_conn *conn, u8 flags, struct byte_view payload, struct send_buf sb,
                                         struct arena arn)
{
    assert(conn);

    // This is correct even if `send_next > send_window + send_unack` because C uses modular arithmetic for
    // unsigned types.
    u32 avail_window = (conn->send_window + conn->send_unack) - conn->send_next;
    sz n_send = MIN((sz)avail_window, payload.len);
    struct byte_view effective_payload = byte_view_new(payload.dat, n_send);

    struct result res = tcp_send_segment_raw(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port,
                                             conn->send_next, conn->recv_next, conn->recv_window, flags,
                                             effective_payload, sb, arn);
    if (res.is_error)
        return result_sz_error(res.code);

    // By advancing `send_next`, we increase the number of bytes in flight.
    conn->send_next += (u32)n_send;
    if (flags & TCP_HDR_FLAG_SYN)
        conn->send_next++;
    if (flags & TCP_HDR_FLAG_FIN)
        conn->send_next++;

    return result_sz_ok(n_send);
}

static inline struct result tcp_send_segment_empty(struct tcp_conn *conn, u8 flags, struct send_buf sb,
                                                   struct arena arn)
{
    assert(conn);
    struct result_sz res = tcp_send_segment(conn, flags, byte_view_new(NULL, 0), sb, arn);
    if (res.is_error)
        return result_error(res.code);
    return result_ok();
}

///////////////////////////////////////////////////////////////////////////////
// Handle incoming segments                                                  //
///////////////////////////////////////////////////////////////////////////////

static struct result tcp_handle_receive_listen(struct tcp_conn *conn, struct ipv4_addr peer_addr, u16 peer_port,
                                               struct tcp_header *hdr, struct byte_view payload, struct send_buf sb,
                                               struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_LISTEN);

    if (!(hdr->flags & TCP_HDR_FLAG_SYN))
        return result_ok(); // We don't care about receiving anything but SYNs when in the LISTEN state.

    // When in LISTEN state, the connection doesn't known about the peer yet. So the peer fields must be wildcards.
    assert(ipv4_addr_is_equal(conn->peer_addr, ipv4_addr_new(0, 0, 0, 0)));
    assert(conn->peer_port == 0);
    conn->peer_addr = peer_addr;
    conn->peer_port = peer_port;

    conn->state = TCP_CONN_STATE_SYN_RCVD;
    // The SYN in the incoming header has consumed one sequence number.
    conn->recv_next = u32_from_net_u32(hdr->seq_num) + payload.len + 1;

    // The outgoing sequence number doesn't need to change because we don't have any payload to send.

    print_dbg(
        PDBG,
        STR("Received SYN for a connection in the LISTEN state (%s). Responding with SYN + ACK. The connection is in the SYN_RCVD state now.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_SYN | TCP_HDR_FLAG_ACK, sb, tmp);
}

static void tcp_handle_receive_syn_rcvd(struct tcp_conn *conn, struct tcp_header *hdr, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_SYN_RCVD);

    // We always send a SYN before moving to the SYN_RCVD state. We don't care about anything at this point but an
    // ACK for the SYN.
    if (!(hdr->flags & TCP_HDR_FLAG_ACK))
        return;

    conn->state = TCP_CONN_STATE_ESTABLISHED;

    tcp_conn_update_unack(conn, u32_from_net_u32(hdr->ack_num));
    conn->send_window = u16_from_net_u16(hdr->window_size);

    print_dbg(
        PDBG,
        STR("Received ACK for a connection in the SYN_RCVD state (%s). Not responding. The connection is ESTABLISHED now.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
}

static struct result tcp_handle_receive_fin_wait_1(struct tcp_conn *conn, struct tcp_header *hdr,
                                                   struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_FIN_WAIT_1);

    if ((hdr->flags & TCP_HDR_FLAG_FIN) && (hdr->flags & TCP_HDR_FLAG_ACK)) {
        conn->state = TCP_CONN_STATE_TIME_WAIT;

        tcp_conn_update_unack(conn, u32_from_net_u32(hdr->ack_num));
        conn->send_window = u16_from_net_u16(hdr->window_size);

        // TODO: Receive logic.
        // The incoming FIN consumed one sequence number.
        conn->recv_next = u32_from_net_u32(hdr->seq_num) + payload.len + 1;

        print_dbg(
            PDBG,
            STR("Received FIN + ACK for a connection in the FIN_WAIT_1 state (%s). Responding with ACK. The connection is in the TIME_WAIT state now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
    } else if (hdr->flags & TCP_HDR_FLAG_FIN) {
        conn->state = TCP_CONN_STATE_CLOSING;

        // TODO: Receive logic.
        // The incoming FIN consumed one sequence number.
        conn->recv_next = u32_from_net_u32(hdr->seq_num) + payload.len + 1;
        conn->send_window = u16_from_net_u16(hdr->window_size);

        print_dbg(
            PDBG,
            STR("Received FIN for a connection in the FIN_WAIT_1 state (%s). Responding with ACK. The connection is in the CLOSING state now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
    } else if (hdr->flags & TCP_HDR_FLAG_ACK) {
        conn->state = TCP_CONN_STATE_FIN_WAIT_2;

        print_dbg(PINFO, STR("DEBUGGING: hdr->ack_num=%u conn->send_next=%u conn->send_unack=%u\n"),
                  u32_from_net_u32(hdr->ack_num), conn->send_next, conn->send_unack);
        tcp_conn_update_unack(conn, u32_from_net_u32(hdr->ack_num));
        conn->send_window = u16_from_net_u16(hdr->window_size);

        print_dbg(
            PDBG,
            STR("Received ACK for a connection in the FIN_WAIT_1 state (%s). Not responding. The connection is in the FIN_WAIT_2 state now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        return result_ok();
    }

    // Stay in the FIN_WAIT_1 state if neither an ACK, nor a FIN, nor both were received.
    return result_ok();
}

static struct result tcp_handle_receive_fin_wait_2(struct tcp_conn *conn, struct tcp_header *hdr,
                                                   struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_FIN_WAIT_2);

    if (!(hdr->flags & TCP_HDR_FLAG_FIN))
        return result_ok(); // Nothing but FINs is of interest in this state.

    conn->state = TCP_CONN_STATE_TIME_WAIT;
    // TODO: Receive logic.
    // The incoming FIN consumed one sequence number.
    conn->recv_next = u32_from_net_u32(hdr->seq_num) + payload.len + 1;

    if (hdr->flags & TCP_HDR_FLAG_ACK)
        tcp_conn_update_unack(conn, u32_from_net_u32(hdr->ack_num));
    conn->send_window = u16_from_net_u16(hdr->window_size);

    print_dbg(
        PDBG,
        STR("Received FIN for a connection in the FIN_WAIT_2 state (%s). Responding with ACK. The connection is in the TIME_WAIT state now.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
}

static void tcp_handle_receive_closing(struct tcp_conn *conn, struct tcp_header *hdr, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_CLOSING);

    if (!(hdr->flags & TCP_HDR_FLAG_ACK))
        return; // Nothing but ACKs is of interest in this state.

    conn->state = TCP_CONN_STATE_TIME_WAIT;

    tcp_conn_update_unack(conn, u32_from_net_u32(hdr->ack_num));
    conn->send_window = u16_from_net_u16(hdr->window_size);

    print_dbg(
        PDBG,
        STR("Received ACK for a connection in the CLOSING state (%s). Not responding. The connection is in the TIME_WAIT state now.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
}

static bool tcp_checksum_is_ok(struct tcp_ip_pseudo_header pseudo_hdr, struct byte_view segment)
{
    net_u16 checksum = net_u16_from_u16(0);
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&pseudo_hdr, sizeof(pseudo_hdr)));
    checksum = internet_checksum_iterate(checksum, segment);
    return internet_checksum_finalize(checksum).inner == 0;
}

struct result tcp_handle_packet(struct tcp_ip_pseudo_header pseudo_hdr, struct byte_view segment, struct send_buf sb,
                                struct arena tmp)
{
    if (segment.len < sizeof(struct tcp_header)) {
        print_dbg(PDBG, STR("Received TCP segment smaller than the TCP header. Dropping ...\n"));
        return result_ok();
    }

    struct tcp_header *tcp_hdr = byte_view_ptr(segment);

    if (!tcp_checksum_is_ok(pseudo_hdr, segment)) {
        print_dbg(PDBG, STR("Received TCP segment with invalid (end-to-end) checksum. Dropping ...\n"));
        crash("Bad checksum\n");
        return result_ok();
    }

    if (tcp_hdr->header_len < TCP_HEADER_LEN_NO_OPT) {
        print_dbg(PDBG,
                  STR("Received TCP segment with invalid header length %hhd (must be at least " TOSTRING(
                      TCP_HEADER_LEN_NO_OPT) "). Dropping ...\n"),
                  tcp_hdr->header_len);
        return result_ok();
    }

    if (tcp_hdr->header_len > TCP_HEADER_LEN_NO_OPT) {
        print_dbg(PWARN, STR("Received TCP segment with options that won't be handled (header_len=%hhd).\n"),
                  tcp_hdr->header_len);
    }

    struct byte_view payload =
        byte_view_new(segment.dat + tcp_hdr->header_len * 4, segment.len - tcp_hdr->header_len * 4);

    struct ipv4_addr host_addr = pseudo_hdr.dest_addr;
    struct ipv4_addr peer_addr = pseudo_hdr.src_addr;
    u16 host_port = u16_from_net_u16(tcp_hdr->dest_port);
    u16 peer_port = u16_from_net_u16(tcp_hdr->src_port);

    struct tcp_conn *conn = tcp_lookup_conn(host_addr, peer_addr, host_port, peer_port);

    if (!conn) {
        print_dbg(PDBG, STR("Could not find a connection for TCP segment from peer (%s). Sending a reset.\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &tmp));
        return tcp_send_segment_raw(host_addr, peer_addr, host_port, peer_port, u32_from_net_u32(tcp_hdr->ack_num),
                                    u32_from_net_u32(tcp_hdr->seq_num), u16_from_net_u16(tcp_hdr->window_size),
                                    TCP_HDR_FLAG_RST, byte_view_new(NULL, 0), sb, tmp);
    }

    switch (conn->state) {
    case TCP_CONN_STATE_LISTEN:
        return tcp_handle_receive_listen(conn, peer_addr, peer_port, tcp_hdr, payload, sb, tmp);
    case TCP_CONN_STATE_SYN_RCVD:
        tcp_handle_receive_syn_rcvd(conn, tcp_hdr, tmp);
        return result_ok();
    case TCP_CONN_STATE_ESTABLISHED:
        crash("TODO: Handle ESTABLISHED");
    case TCP_CONN_STATE_CLOSE_WAIT:
        crash("TODO: Handle CLOSE_WAIT");
    case TCP_CONN_STATE_FIN_WAIT_1:
        return tcp_handle_receive_fin_wait_1(conn, tcp_hdr, payload, sb, tmp);
    case TCP_CONN_STATE_FIN_WAIT_2:
        return tcp_handle_receive_fin_wait_2(conn, tcp_hdr, payload, sb, tmp);
    case TCP_CONN_STATE_CLOSING:
        tcp_handle_receive_closing(conn, tcp_hdr, tmp);
        return result_ok();
    default:
        print_dbg(PERROR, STR("Unknown connection state %d for %s.\n"), conn->state,
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &tmp));
        crash("Connection state invalid");
    }
}

///////////////////////////////////////////////////////////////////////////////
// User interface                                                            //
///////////////////////////////////////////////////////////////////////////////

static struct result_u32 tcp_generate_isn(void)
{
    u64 isn_raw;
    bool success = rdrand_u64(&isn_raw);
    if (!success)
        return result_u32_error(EIO);
    return result_u32_ok((u32)isn_raw);
}

struct tcp_conn *tcp_conn_listen_accept(struct ipv4_addr addr, u16 port, struct arena tmp)
{
    struct tcp_conn *conn = tcp_lookup_conn(addr, ipv4_addr_new(0, 0, 0, 0), port, 0);

    if (!conn) {
        conn = tcp_alloc_conn();
        if (!conn) {
            print_dbg(PERROR, STR("Failed to allocate new TCP connection (addr=%s, port=%hu).\n"),
                      ipv4_addr_format(addr, &tmp), port);
            return NULL;
        }

        struct result_u32 isn_res = tcp_generate_isn();
        if (isn_res.is_error) {
            tcp_free_conn(conn);
            return NULL;
        }

        conn->host_addr = addr;
        conn->peer_addr = ipv4_addr_new(0, 0, 0, 0);
        conn->host_port = port;
        conn->peer_port = 0;
        conn->state = TCP_CONN_STATE_LISTEN;

        // TODO: Receive logic.
        conn->recv_next = 0;
        conn->recv_window = 0x1000;

        conn->iss = result_u32_checked(isn_res);
        conn->send_unack = conn->iss;
        conn->send_next = conn->iss;
        conn->send_window = 0;

        print_dbg(PINFO, STR("New connection in LISTEN state on addr=%s port=%hu ...\n"), ipv4_addr_format(addr, &tmp),
                  port);

        return NULL;
    }

    if (conn->state == TCP_CONN_STATE_ESTABLISHED)
        return conn;

    return NULL;
}

// Send `payload` to the other side of the connection `conn`. The return value indicates the number of bytes we were
// able to transmit.
struct result_sz tcp_conn_send(struct tcp_conn *conn, struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    assert(conn);
    return tcp_send_segment(conn, TCP_HDR_FLAG_ACK, payload, sb, tmp);
}

// Close the connection `*conn`. `conn` will be set to NULL since it's stale now.
struct result tcp_conn_close(struct tcp_conn **conn_ptr, struct send_buf sb, struct arena tmp)
{
    assert(conn_ptr);
    assert(*conn_ptr);

    struct tcp_conn *conn = *conn_ptr;
    assert(conn->state == TCP_CONN_STATE_ESTABLISHED); // If this doesn't hold, we made a mistake in the user API.

    conn->state = TCP_CONN_STATE_FIN_WAIT_1;
    // No need to change the outgoing sequence number because there is no payload to send.

    *conn_ptr = NULL;

    // TODO: I don't get why we need to send an ACK here ...
    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_FIN | TCP_HDR_FLAG_ACK, sb, tmp);
}
