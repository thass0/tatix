#include <tx/net/tcp.h>

#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/byte.h>
#include <tx/kvalloc.h>
#include <tx/list.h>
#include <tx/net/ip.h>
#include <tx/net/netorder.h>
#include <tx/print.h>
#include <tx/time.h>

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
// Circular buffer implementation                                            //
///////////////////////////////////////////////////////////////////////////////

struct circ_buf {
    struct byte_array data;
    sz head;
    sz tail;
};

static inline bool circ_buf_is_empty(struct circ_buf *buf)
{
    assert(buf);
    return buf->head == buf->tail;
}

static inline bool circ_buf_is_full(struct circ_buf *buf)
{
    assert(buf);
    return (buf->head + 1) % buf->data.len == buf->tail;
}

static inline sz circ_buf_count(struct circ_buf *buf)
{
    assert(buf);
    assert(buf->data.len);
    return (buf->head - buf->tail + buf->data.len) % buf->data.len;
}

static inline sz circ_buf_space(struct circ_buf *buf)
{
    assert(buf);
    return buf->data.len - 1 - circ_buf_count(buf);
}

static struct result circ_buf_alloc(struct circ_buf *buf, sz capacity)
{
    assert(buf);
    assert(capacity > 0);

    struct option_byte_array mem_opt = kvalloc_alloc(capacity, 1);
    if (mem_opt.is_none)
        return result_error(ENOMEM);

    buf->data = option_byte_array_checked(mem_opt);
    buf->head = 0;
    buf->tail = 0;

    return result_ok();
}

static void circ_buf_free(struct circ_buf *buf)
{
    assert(buf);

    kvalloc_free(buf->data);
    buf->data = byte_array_new(NULL, 0);
}

static struct result circ_buf_push_byte(struct circ_buf *buf, byte b)
{
    assert(buf);

    if (circ_buf_is_full(buf))
        return result_error(EAGAIN);

    buf->data.dat[buf->head] = b;
    buf->head = (buf->head + 1) % buf->data.len;
    return result_ok();
}

static struct result_byte circ_buf_pop_byte(struct circ_buf *buf)
{
    assert(buf);

    if (circ_buf_is_empty(buf))
        return result_byte_error(EAGAIN);

    byte b = buf->data.dat[buf->tail];
    buf->tail = (buf->tail + 1) % buf->data.len;
    return result_byte_ok(b);
}

static struct result circ_buf_write(struct circ_buf *buf, struct byte_view data)
{
    assert(buf);

    if (data.len > circ_buf_space(buf))
        return result_error(EAGAIN);

    for (sz i = 0; i < data.len; i++) {
        struct result res = circ_buf_push_byte(buf, data.dat[i]);
        if (res.is_error)
            return res;
    }

    return result_ok();
}

static sz circ_buf_read(struct circ_buf *buf, struct byte_buf *dest)
{
    assert(buf);
    assert(dest);

    sz bytes_read = 0;
    sz available = circ_buf_count(buf);
    sz space = dest->cap - dest->len;
    sz to_read = MIN(available, space);

    for (sz i = 0; i < to_read; i++) {
        struct result_byte res = circ_buf_pop_byte(buf);
        if (res.is_error)
            break;

        if (byte_buf_append_n(dest, 1, result_byte_checked(res)) != 1)
            break;

        bytes_read++;
    }

    return bytes_read;
}

///////////////////////////////////////////////////////////////////////////////
// Manage connections                                                        //
///////////////////////////////////////////////////////////////////////////////

enum tcp_conn_state {
    TCP_CONN_STATE_LISTEN, // Waiting for a client to send a SYN for the connection.
    TCP_CONN_STATE_SYN_RCVD,
    TCP_CONN_STATE_ESTABLISHED,
    TCP_CONN_STATE_CLOSE_WAIT,
    TCP_CONN_STATE_LAST_ACK,
    TCP_CONN_STATE_FIN_WAIT_1,
    TCP_CONN_STATE_FIN_WAIT_2,
    TCP_CONN_STATE_CLOSING,
    TCP_CONN_STATE_TIME_WAIT,

    TCP_CONN_STATE_RESET // Special state that's not included in the normal TCP state transitions.
};

#define TCP_CONN_TIME_WAIT_MS 100 /* This is low so we can re-use connections quickly. */
#define TCP_CONN_RECV_WINDOW_SIZE 0x2000

struct tcp_conn {
    bool is_used;
    struct ipv4_addr host_addr;
    struct ipv4_addr peer_addr;
    u16 host_port;
    u16 peer_port;
    enum tcp_conn_state state;

    struct dlist accept_queue;

    // Transmission
    u32 send_unack; // SND.UNA
    u32 send_next; // SND.NXT
    u16 send_window; // SND.WND
    u32 iss; // Initial send sequence number (ISS).

    // Reception
    u32 recv_next; // RCV.NXT
    u16 recv_window; // RCV.WND
    struct circ_buf recv_buf;

    // Set when the connection is put in the TIME_WAIT state. The connection is deleted when `TCP_CONN_TIME_WAIT_MS`
    // has passed (see `tcp_purge_old_conn` and calls sites of this function).
    struct time_ms time_wait_start;
};

// If we need to handle more connections at the same time, we could also allocate this array dynamically. The main
// reason for using an array is that it's simple to search (without requiring much pointer chasing like linked lists
// do).
#define TCP_CONN_MAX_NUM 64
static struct tcp_conn global_tcp_conn_table[TCP_CONN_MAX_NUM];

static void tcp_free_conn(struct tcp_conn *conn)
{
    assert(conn);

    circ_buf_free(&conn->recv_buf);
    dlist_remove(&conn->accept_queue);

    // Since we are reusing these, we want to make sure we don't accidentally reuse old data. Thus we set each
    // structure to an easy to recognize bit pattern.
    byte_array_set(byte_array_new((void *)conn, sizeof(*conn)), 0xee);
    conn->is_used = false;
}

static inline void tcp_purge_old_conn(void)
{
    for (sz i = 0; i < TCP_CONN_MAX_NUM; i++) {
        struct tcp_conn *conn = &global_tcp_conn_table[i];

        if (conn->is_used && conn->state == TCP_CONN_STATE_TIME_WAIT) {
            if (time_current_ms().ms >= conn->time_wait_start.ms) {
                tcp_free_conn(conn);
            }
        }
    }
}

static struct tcp_conn *tcp_alloc_conn(void)
{
    tcp_purge_old_conn();

    for (sz i = 0; i < TCP_CONN_MAX_NUM; i++) {
        struct tcp_conn *conn = &global_tcp_conn_table[i];

        if (!conn->is_used) {
            conn->is_used = true;
            return conn;
        }
    }

    return NULL;
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
                                        u16 peer_port, bool use_peer_wildcards)
{
    tcp_purge_old_conn();

    for (sz i = 0; i < TCP_CONN_MAX_NUM; i++) {
        struct tcp_conn *conn = &global_tcp_conn_table[i];
        if (!conn->is_used)
            continue;

        if (use_peer_wildcards) {
            if (ipv4_addr_is_equal(host_addr, conn->host_addr) &&
                ipv4_addr_wildcard_compare(peer_addr, conn->peer_addr) && host_port == conn->host_port &&
                port_wildcard_compare(peer_port, conn->peer_port))
                return conn;
        } else {
            if (ipv4_addr_is_equal(host_addr, conn->host_addr) && ipv4_addr_is_equal(peer_addr, conn->peer_addr) &&
                host_port == conn->host_port && peer_port == conn->peer_port)
                return conn;
        }
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

static struct result_u32 tcp_generate_isn(void)
{
    u64 isn_raw;
    bool success = rdrand_u64(&isn_raw);
    if (!success)
        return result_u32_error(EIO);
    return result_u32_ok((u32)isn_raw);
}

static struct tcp_conn *tcp_conn_alloc_and_init(struct ipv4_addr host_addr, u16 host_port, enum tcp_conn_state state)
{
    struct tcp_conn *conn = tcp_alloc_conn();
    if (!conn)
        return NULL;

    struct result_u32 isn_res = tcp_generate_isn();
    if (isn_res.is_error) {
        tcp_free_conn(conn);
        return NULL;
    }

    conn->host_addr = host_addr;
    conn->peer_addr = ipv4_addr_new(0, 0, 0, 0);
    conn->host_port = host_port;
    conn->peer_port = 0;
    conn->state = state;

    dlist_init_empty(&conn->accept_queue);

    conn->recv_next = 0;
    conn->recv_window = TCP_CONN_RECV_WINDOW_SIZE;
    conn->recv_buf.data = byte_array_new(NULL, 0);
    conn->recv_buf.head = 0;
    conn->recv_buf.tail = 0;

    conn->iss = result_u32_checked(isn_res);
    conn->send_unack = conn->iss;
    conn->send_next = conn->iss;
    conn->send_window = 0;

    conn->time_wait_start = time_ms_new(0);

    return conn;
}

static bool seq_gt(u32 a, u32 b)
{
    return (i64)a - (i64)b > 0;
}

static void tcp_conn_update_send_state(struct tcp_conn *conn, struct tcp_header *hdr)
{
    assert(conn);

    if (hdr->flags & TCP_HDR_FLAG_ACK) {
        u32 ack_num = u32_from_net_u32(hdr->ack_num);
        if (seq_gt(ack_num, conn->send_unack))
            conn->send_unack = ack_num;
    }

    conn->send_window = u16_from_net_u16(hdr->window_size);
}

static sz tcp_conn_update_recv_state(struct tcp_conn *conn, struct tcp_header *hdr, struct byte_view payload,
                                     struct arena tmp)
{
    assert(conn);
    assert(hdr);

    u32 seq_num = u32_from_net_u32(hdr->seq_num);

    if (payload.len > 0) {
        if (seq_num != conn->recv_next) {
            // We can support out-of-order delivery at a later time.
            print_dbg(PDBG, STR("Out-of-order segment received: expected seq=%u, got seq=%u (%s). Dropping ...\n"),
                      conn->recv_next, seq_num,
                      tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
            return 0;
        }

        struct result write_res = circ_buf_write(&conn->recv_buf, payload);
        if (write_res.is_error) {
            assert(write_res.code == EAGAIN);
            print_dbg(PWARN, STR("Not enough space in receive buffer to receive incoming segment (%s). Dropping ...\n"),
                      tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
            return 0;
        }

        conn->recv_next += payload.len;
    }

    if (hdr->flags & TCP_HDR_FLAG_FIN) {
        if (seq_num + payload.len != conn->recv_next) {
            print_dbg(PDBG, STR("FIN received with unexpected sequence number (%s). Dropping ...\n"),
                      tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
            return payload.len;
        }

        conn->recv_next++; // The incoming FIN consumed one sequence number.
    }

    return payload.len;
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

static inline u32 tcp_send_window_avail(struct tcp_conn *conn)
{
    // This is correct even if `send_next > send_window + send_unack` because C uses modular arithmetic for
    // unsigned types.
    return (conn->send_window + conn->send_unack) - conn->send_next;
}

static struct result_sz tcp_send_segment(struct tcp_conn *conn, u8 flags, struct byte_view payload, struct send_buf sb,
                                         struct arena arn)
{
    assert(conn);

    sz n_send = MIN((sz)tcp_send_window_avail(conn), payload.len);
    struct byte_view effective_payload = byte_view_new(payload.dat, n_send);

    // NOTE: We must send segments even if `n_send` is 0. This is for control segments, usually.

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

static struct result tcp_handle_receive_listen(struct tcp_conn *listen_conn, struct ipv4_addr peer_addr, u16 peer_port,
                                               struct tcp_header *hdr, struct send_buf sb, struct arena tmp)
{
    assert(listen_conn);
    assert(hdr);
    assert(listen_conn->state == TCP_CONN_STATE_LISTEN);

    // We ignore RST for connections in the listen state because these connections aren't really connected to anything
    // yet (they're just waiting to connect).
    if (hdr->flags & TCP_HDR_FLAG_RST)
        return result_ok();

    if (!(hdr->flags & TCP_HDR_FLAG_SYN))
        return result_ok(); // We don't care about receiving anything but SYNs when in the LISTEN state.

    // When in LISTEN state, the connection doesn't known about the peer yet. So the peer fields must be wildcards.
    assert(ipv4_addr_is_equal(listen_conn->peer_addr, ipv4_addr_new(0, 0, 0, 0)));
    assert(listen_conn->peer_port == 0);

    // A new connection is created now so that `listen_conn` can remain in the LISTEN state to accept new connections.
    // The new connection will be moved through the states of the TCP handshake until it's in the ESTABLISHED state.

    struct tcp_conn *conn =
        tcp_conn_alloc_and_init(listen_conn->host_addr, listen_conn->host_port, TCP_CONN_STATE_SYN_RCVD);
    if (!conn) {
        print_dbg(PDBG, STR("Failed to allocate and initialize new SYN_RCVD TCP connection (%s).\n"),
                  tcp_fmt_conn(listen_conn->host_addr, peer_addr, listen_conn->host_port, peer_port, &tmp));
        return result_error(ENOMEM);
    }

    dlist_insert(&listen_conn->accept_queue, &conn->accept_queue);

    conn->peer_addr = peer_addr;
    conn->peer_port = peer_port;
    // The SYN in the incoming header has consumed one sequence number so we add one to the ISN send by our peer.
    conn->recv_next = u32_from_net_u32(hdr->seq_num) + 1;

    print_dbg(
        PDBG,
        STR("Received SYN for a connection in the LISTEN state (%s). Responding with SYN + ACK. Created a new connection in the SYN_RCVD state.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_SYN | TCP_HDR_FLAG_ACK, sb, tmp);
}

static struct result tcp_handle_receive_syn_rcvd(struct tcp_conn *conn, struct tcp_header *hdr, struct send_buf sb,
                                                 struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_SYN_RCVD);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        // Connections in the SYN_RECV state are in the processes of establishing the connection. They can't be used
        // yet and users of the TCP API can't access them. Thus, we just delete the connection.
        tcp_free_conn(conn);
        return result_ok();
    }

    // We always send a SYN before moving to the SYN_RCVD state. We don't care about anything at this point but an
    // ACK for the SYN.
    if (!(hdr->flags & TCP_HDR_FLAG_ACK))
        return result_ok();

    conn->state = TCP_CONN_STATE_ESTABLISHED;
    tcp_conn_update_send_state(conn, hdr);

    // We start receiving data in the ESTABLISHED state so we need to allocate a buffer at this point.
    struct result buf_alloc_res = circ_buf_alloc(&conn->recv_buf, conn->recv_window);
    if (buf_alloc_res.is_error) {
        print_dbg(
            PWARN,
            STR("Failed to allocate receive buffer for a connection (%s). Resetting and deleting the connection.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
        tcp_send_segment_empty(conn, TCP_HDR_FLAG_RST, sb, tmp);
        tcp_free_conn(conn);
        return result_error(ENOMEM);
    }

    print_dbg(
        PDBG,
        STR("Received ACK for a connection in the SYN_RCVD state (%s). Not responding. The connection is ESTABLISHED now.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

    return result_ok();
}

static struct result tcp_handle_receive_established(struct tcp_conn *conn, struct tcp_header *hdr,
                                                    struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_ESTABLISHED);

    tcp_conn_update_send_state(conn, hdr);
    sz n_received = tcp_conn_update_recv_state(conn, hdr, payload, tmp);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        conn->state = TCP_CONN_STATE_RESET;

        print_dbg(
            PDBG,
            STR("Received RST for a connection in the ESTABLISHED state (%s). Not responding. The connection is in the RESET state now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        return result_ok();
    }

    if (hdr->flags & TCP_HDR_FLAG_FIN) {
        conn->state = TCP_CONN_STATE_CLOSE_WAIT;

        print_dbg(
            PDBG,
            STR("Received FIN for a connection in the ESTABLISHED state (%s). Responding with ACK. The connection is in the CLOSE_WAIT state now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
    }

    if (n_received > 0) {
        print_dbg(PDBG, STR("Received %ld bytes of data for connection %s. Responding with ACK.\n"), n_received,
                  tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
    }

    return result_ok();
}

static void tcp_handle_receive_last_ack(struct tcp_conn *conn, struct tcp_header *hdr, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_LAST_ACK);

    if ((hdr->flags & TCP_HDR_FLAG_ACK) || (hdr->flags & TCP_HDR_FLAG_RST)) {
        print_dbg(
            PDBG,
            STR("Received an ACK or RST (flags=%hhu) for a connection in the LAST_ACK state (%s). Not responding. The connection is deleted now."),
            hdr->flags, tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        tcp_free_conn(conn);
    }
}

static struct result tcp_handle_receive_fin_wait_1(struct tcp_conn *conn, struct tcp_header *hdr,
                                                   struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_FIN_WAIT_1);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the FIN_WAIT_1 state (%s). Not responding. The connection is deleted now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
        tcp_free_conn(conn);
        return result_ok();
    }

    if ((hdr->flags & TCP_HDR_FLAG_FIN) && (hdr->flags & TCP_HDR_FLAG_ACK)) {
        conn->state = TCP_CONN_STATE_TIME_WAIT;
        conn->time_wait_start = time_current_ms();
        tcp_conn_update_send_state(conn, hdr);
        tcp_conn_update_recv_state(conn, hdr, payload, tmp);

        print_dbg(
            PDBG,
            STR("Received FIN + ACK for a connection in the FIN_WAIT_1 state (%s). Responding with ACK. The connection is in the TIME_WAIT state now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
    } else if (hdr->flags & TCP_HDR_FLAG_FIN) {
        conn->state = TCP_CONN_STATE_CLOSING;
        tcp_conn_update_send_state(conn, hdr);
        tcp_conn_update_recv_state(conn, hdr, payload, tmp);

        print_dbg(
            PDBG,
            STR("Received FIN for a connection in the FIN_WAIT_1 state (%s). Responding with ACK. The connection is in the CLOSING state now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
    } else if (hdr->flags & TCP_HDR_FLAG_ACK) {
        conn->state = TCP_CONN_STATE_FIN_WAIT_2;
        tcp_conn_update_send_state(conn, hdr);
        tcp_conn_update_recv_state(conn, hdr, payload, tmp);

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

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the FIN_WAIT_2 state (%s). Not responding. The connection is deleted now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
        tcp_free_conn(conn);
        return result_ok();
    }

    if (!(hdr->flags & TCP_HDR_FLAG_FIN))
        return result_ok();

    conn->state = TCP_CONN_STATE_TIME_WAIT;
    conn->time_wait_start = time_current_ms();
    tcp_conn_update_send_state(conn, hdr);
    tcp_conn_update_recv_state(conn, hdr, payload, tmp);

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

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the CLOSING state (%s). Not responding. The connection is deleted now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
        tcp_free_conn(conn);
        return;
    }

    if (!(hdr->flags & TCP_HDR_FLAG_ACK))
        return;

    conn->state = TCP_CONN_STATE_TIME_WAIT;
    conn->time_wait_start = time_current_ms();
    tcp_conn_update_send_state(conn, hdr);

    print_dbg(
        PDBG,
        STR("Received ACK for a connection in the CLOSING state (%s). Not responding. The connection is in the TIME_WAIT state now.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
}

static struct result tcp_handle_receive_time_wait(struct tcp_conn *conn, struct tcp_header *hdr,
                                                  struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_TIME_WAIT);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the TIME_WAIT state (%s). Not responding. The connection is deleted now.\n"),
            tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));
        tcp_free_conn(conn);
        return result_ok();
    }

    // No user will ever see that data that we receive here. The only purpose of updating the send and receive states
    // at this point is to make sure that we ACK everything that the peer sent us when returning from this function.
    tcp_conn_update_send_state(conn, hdr);
    tcp_conn_update_recv_state(conn, hdr, payload, tmp);

    if (!(hdr->flags & TCP_HDR_FLAG_FIN))
        return result_ok();

    print_dbg(
        PDBG,
        STR("Received FIN for a connection in the TIME_WAIT state (%s). Responding with ACK. The connection remains in the TIME_WAIT state.\n"),
        tcp_fmt_conn(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, &tmp));

    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, sb, tmp);
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

    struct byte_view payload = byte_view_skip(segment, tcp_hdr->header_len * 4);

    struct ipv4_addr host_addr = pseudo_hdr.dest_addr;
    struct ipv4_addr peer_addr = pseudo_hdr.src_addr;
    u16 host_port = u16_from_net_u16(tcp_hdr->dest_port);
    u16 peer_port = u16_from_net_u16(tcp_hdr->src_port);

    // TODO: It's somewhat wasteful to run the `tcp_lookup_conn` function twice.
    struct tcp_conn *conn = tcp_lookup_conn(host_addr, peer_addr, host_port, peer_port, false);

    // Try again if we weren't able to find a connection but this time consider wildcard matches.
    if (!conn)
        conn = tcp_lookup_conn(host_addr, peer_addr, host_port, peer_port, true);

    if (!conn) {
        print_dbg(PDBG, STR("Could not find a connection for TCP segment from peer (%s). Sending a reset.\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &tmp));
        return tcp_send_segment_raw(host_addr, peer_addr, host_port, peer_port, u32_from_net_u32(tcp_hdr->ack_num),
                                    u32_from_net_u32(tcp_hdr->seq_num), u16_from_net_u16(tcp_hdr->window_size),
                                    TCP_HDR_FLAG_RST, byte_view_new(NULL, 0), sb, tmp);
    }

    switch (conn->state) {
    case TCP_CONN_STATE_LISTEN:
        return tcp_handle_receive_listen(conn, peer_addr, peer_port, tcp_hdr, sb, tmp);
    case TCP_CONN_STATE_SYN_RCVD:
        return tcp_handle_receive_syn_rcvd(conn, tcp_hdr, sb, tmp);
    case TCP_CONN_STATE_ESTABLISHED:
        return tcp_handle_receive_established(conn, tcp_hdr, payload, sb, tmp);
    case TCP_CONN_STATE_CLOSE_WAIT:
        return result_ok(); // We are just waiting for the user to close the connection. There is nothing to do.
    case TCP_CONN_STATE_LAST_ACK:
        tcp_handle_receive_last_ack(conn, tcp_hdr, tmp);
        return result_ok();
    case TCP_CONN_STATE_FIN_WAIT_1:
        return tcp_handle_receive_fin_wait_1(conn, tcp_hdr, payload, sb, tmp);
    case TCP_CONN_STATE_FIN_WAIT_2:
        return tcp_handle_receive_fin_wait_2(conn, tcp_hdr, payload, sb, tmp);
    case TCP_CONN_STATE_CLOSING:
        tcp_handle_receive_closing(conn, tcp_hdr, tmp);
        return result_ok();
    case TCP_CONN_STATE_TIME_WAIT:
        return tcp_handle_receive_time_wait(conn, tcp_hdr, payload, sb, tmp);
    case TCP_CONN_STATE_RESET:
        return result_ok(); // We are just waiting for the user to close the connection. There is nothing to do.
    default:
        print_dbg(PERROR, STR("Unknown connection state %d for %s.\n"), conn->state,
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &tmp));
        crash("Connection state invalid");
    }
}

///////////////////////////////////////////////////////////////////////////////
// User interface                                                            //
///////////////////////////////////////////////////////////////////////////////

struct tcp_conn *tcp_conn_listen(struct ipv4_addr addr, u16 port, struct arena tmp)
{
    struct tcp_conn *conn = tcp_lookup_conn(addr, ipv4_addr_new(0, 0, 0, 0), port, 0, true);

    if (conn && conn->state == TCP_CONN_STATE_LISTEN)
        return conn;

    conn = tcp_conn_alloc_and_init(addr, port, TCP_CONN_STATE_LISTEN);
    if (!conn) {
        print_dbg(PERROR, STR("Failed to allocate and initialize new LISTEN TCP connection (%s:%hu).\n"),
                  ipv4_addr_format(addr, &tmp), port);
        return NULL;
    }

    print_dbg(PINFO, STR("New connection in LISTEN state on %s:%hu ...\n"), ipv4_addr_format(addr, &tmp), port);

    return conn;
}

struct tcp_conn *tcp_conn_accept(struct tcp_conn *listen_conn)
{
    assert(listen_conn);

    struct tcp_conn *conn = __container_of(listen_conn->accept_queue.next, struct tcp_conn, accept_queue);

    if (conn == listen_conn)
        return NULL;

    // Depending on the timing, the user may call accept when a SYN has been received but before the handshake was
    // completed. In that case the connection is in the SYN_RCVD state, but it's not ready to receive data.
    if (conn->state != TCP_CONN_STATE_ESTABLISHED)
        return NULL;

    dlist_remove(&conn->accept_queue);

    return conn;
}

static inline bool tcp_conn_closed_by_peer(enum tcp_conn_state state)
{
    return (state == TCP_CONN_STATE_CLOSE_WAIT) || (state == TCP_CONN_STATE_RESET);
}

struct result_sz tcp_conn_send(struct tcp_conn *conn, struct byte_view payload, bool *peer_closed_conn,
                               struct send_buf sb, struct arena tmp)
{
    assert(conn);
    assert(peer_closed_conn);

    *peer_closed_conn = tcp_conn_closed_by_peer(conn->state);

    struct result_sz mtu_res = ipv4_route_mtu(conn->peer_addr);
    if (mtu_res.is_error)
        return result_sz_error(mtu_res.code);
    sz mtu = MAX(0, result_sz_checked(mtu_res) - sizeof(struct tcp_header));

    if (tcp_send_window_avail(conn) == 0)
        return result_sz_ok(0);

    struct byte_view fragment = byte_view_new(payload.dat, MIN(payload.len, mtu));
    return tcp_send_segment(conn, TCP_HDR_FLAG_ACK, fragment, sb, tmp);
}

struct result_sz tcp_conn_recv(struct tcp_conn *conn, struct byte_buf *buf, bool *peer_closed_conn)
{
    assert(conn);
    assert(buf);
    assert(peer_closed_conn);

    *peer_closed_conn = tcp_conn_closed_by_peer(conn->state);

    sz avail = circ_buf_count(&conn->recv_buf);
    if (!avail)
        return result_sz_ok(0);

    circ_buf_read(&conn->recv_buf, buf);

    return result_sz_ok(avail);
}

struct result tcp_conn_close(struct tcp_conn **conn_ptr, struct send_buf sb, struct arena tmp)
{
    assert(conn_ptr);
    assert(*conn_ptr);

    struct tcp_conn *conn = *conn_ptr;
    *conn_ptr = NULL;

    if (conn->state == TCP_CONN_STATE_LISTEN || conn->state == TCP_CONN_STATE_SYN_RCVD ||
        conn->state == TCP_CONN_STATE_RESET) {
        tcp_free_conn(conn);
        return result_ok();
    }

    if (conn->state == TCP_CONN_STATE_ESTABLISHED) {
        conn->state = TCP_CONN_STATE_FIN_WAIT_1;

        // The user can't access the connection any more at this point. But it isn't deallocated until all ACKs have
        // completed and the TIME_WAIT period has passed. The connections in the TIME_WAIT state are purged periodically
        // when looking up or allocating connections.

        // TODO: I don't get why we need to send an ACK here ... (but connections don't close correctly without it).
        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_FIN | TCP_HDR_FLAG_ACK, sb, tmp);
    }

    if (conn->state == TCP_CONN_STATE_CLOSE_WAIT) {
        conn->state = TCP_CONN_STATE_LAST_ACK;

        // The user has now lost access to this connection. We are only waiting to receive an ACK from the peer for
        // this FIN and then the connection will be deleted.
        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_FIN | TCP_HDR_FLAG_ACK, sb, tmp);
    }

    // All other states mean that a close operation is already in progress so we don't need to act.

    return result_ok();
}
