#include <tx/net/tcp.h>

#include <tx/assert.h>
#include <tx/byte.h>
#include <tx/kvalloc.h>
#include <tx/list.h>
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
#define TCP_HDR_FLAG_ACK BIT(4)

static_assert(sizeof(struct tcp_header) == 20);

///////////////////////////////////////////////////////////////////////////////
// Manage sockets                                                            //
///////////////////////////////////////////////////////////////////////////////

enum tcp_socket_state {
    TCP_SOCKET_STATE_LISTENING,
};

struct tcp_socket {
    bool is_used;
    struct ipv4_addr addr;
    u16 port;
    enum tcp_socket_state state;
    struct dlist conn_list_head;
};

#define TCP_SOCKET_MAX_NUM 64
static struct tcp_socket global_tcp_socket_table[TCP_SOCKET_MAX_NUM];

static struct tcp_socket *tcp_alloc_socket(void)
{
    for (sz i = 0; i < TCP_SOCKET_MAX_NUM; i++) {
        struct tcp_socket *socket = &global_tcp_socket_table[i];
        if (!socket->is_used) {
            socket->is_used = true;
            return socket;
        }
    }

    return NULL;
}

static struct tcp_socket *tcp_lookup_socket(struct ipv4_addr addr, u16 port)
{
    for (sz i = 0; i < TCP_SOCKET_MAX_NUM; i++) {
        struct tcp_socket *socket = &global_tcp_socket_table[i];
        if (!socket->is_used)
            continue;
        if (ipv4_addr_is_equal(addr, socket->addr) && port == socket->port)
            return socket;
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Manage connections                                                        //
///////////////////////////////////////////////////////////////////////////////

enum tcp_conn_state {
    TCP_CONN_STATE_LISTENING, // Waiting for a client to send a SYN for the connection.
    TCP_CONN_STATE_SYN_RECEIVED,
    TCP_CONN_STATE_ESTABLISHED,
    TCP_CONN_STATE_CLOSING, // We are waiting for the other side to ACK a FIN that we sent it.
};

struct tcp_conn {
    bool is_used;
    struct tcp_socket *socket;
    struct ipv4_addr peer_addr;
    u16 peer_port;
    u32 seq_num;
    u32 ack_num;
    u16 window_size;
    enum tcp_conn_state state;
    struct dlist socket_conn_list;
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

static struct tcp_conn *tcp_lookup_conn(struct tcp_socket *socket, struct ipv4_addr peer_addr, u16 peer_port)
{
    assert(socket);

    if (dlist_is_empty(&socket->conn_list_head))
        return NULL;

    struct dlist *list = socket->conn_list_head.next;
    while (list != &socket->conn_list_head) {
        struct tcp_conn *conn = __container_of(list, struct tcp_conn, socket_conn_list);
        if (ipv4_addr_is_equal(peer_addr, conn->peer_addr) && peer_port == conn->peer_port)
            return conn;
        list = list->next;
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

///////////////////////////////////////////////////////////////////////////////
// Handle incoming segments                                                  //
///////////////////////////////////////////////////////////////////////////////

static bool tcp_checksum_is_ok(struct tcp_ip_pseudo_header pseudo_hdr, struct byte_view segment)
{
    net_u16 checksum = net_u16_from_u16(0);
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&pseudo_hdr, sizeof(pseudo_hdr)));
    checksum = internet_checksum_iterate(checksum, segment);
    return internet_checksum_finalize(checksum).inner == 0;
}

static struct result tcp_send_segment(struct tcp_conn *conn, u8 flags, struct send_buf sb, struct arena arn)
{
    assert(conn);

    // We need this to compute the checksum over the pseudo header because the pseudo header contains information
    // from the IP layer.
    struct result_ipv4_addr interface_addr_res = ipv4_route_interface_addr(conn->peer_addr);
    if (interface_addr_res.is_error)
        return result_error(interface_addr_res.code);
    struct ipv4_addr interface_addr = result_ipv4_addr_checked(interface_addr_res);

    struct tcp_header hdr;
    hdr.src_port = net_u16_from_u16(conn->socket->port);
    hdr.dest_port = net_u16_from_u16(conn->peer_port);
    hdr.seq_num = net_u32_from_u32(conn->seq_num);
    hdr.ack_num = net_u32_from_u32(conn->ack_num);
    hdr.header_len = TCP_HEADER_LEN_NO_OPT;
    hdr.reserved = 0;
    hdr.flags = flags;
    hdr.window_size = net_u16_from_u16(conn->window_size);
    hdr.checksum = net_u16_from_u16(0);
    hdr.urgent = net_u16_from_u16(0);

    struct tcp_ip_pseudo_header pseudo_hdr;
    pseudo_hdr.src_addr = interface_addr;
    pseudo_hdr.dest_addr = conn->peer_addr;
    pseudo_hdr.zero = 0;
    pseudo_hdr.protocol = IPV4_PROTOCOL_TCP;
    pseudo_hdr.tcp_length = net_u16_from_u16(sizeof(hdr));

    net_u16 checksum = net_u16_from_u16(0);
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&hdr, sizeof(hdr)));
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&pseudo_hdr, sizeof(pseudo_hdr)));

    hdr.checksum = internet_checksum_finalize(checksum);

    assert(tcp_checksum_is_ok(pseudo_hdr, byte_view_new((void *)&hdr, sizeof(hdr))));

    struct byte_buf *buf = send_buf_prepend(&sb, sizeof(hdr));
    if (!buf)
        return result_error(ENOMEM);

    assert(byte_buf_append(buf, byte_view_new((void *)&hdr, sizeof(hdr))) == sizeof(hdr));

    return ipv4_send_packet(conn->peer_addr, IPV4_PROTOCOL_TCP, sb, arn);
}

static struct result tcp_handle_receive_syn(struct ipv4_addr host_addr, struct ipv4_addr peer_addr,
                                            struct tcp_header *hdr, struct send_buf sb, struct arena arn)
{
    assert(hdr);

    u16 host_port = u16_from_net_u16(hdr->dest_port);
    u16 peer_port = u16_from_net_u16(hdr->src_port);

    struct tcp_socket *socket = tcp_lookup_socket(host_addr, host_port);
    if (!socket) {
        print_dbg(PDBG, STR("Received SYN from peer for a socket that doesn't exist (%s). Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    if (socket->state != TCP_SOCKET_STATE_LISTENING) {
        print_dbg(PDBG, STR("Received SYN from peer for a socket that's not listening (%s state=%d). Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn), socket->state);
        return result_ok();
    }

    struct tcp_conn *conn = tcp_lookup_conn(socket, peer_addr, peer_port);
    if (conn) {
        print_dbg(PERROR, STR("Received SYN from peer that's already connected (%s). Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    conn = tcp_alloc_conn();
    if (!conn) {
        print_dbg(PERROR, STR("Failed to allocate new TCP connection for %s. Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    conn->socket = socket;
    conn->peer_addr = peer_addr;
    conn->peer_port = peer_port;
    conn->seq_num = 0; // TODO: Choose a random value.
    conn->ack_num = u32_from_net_u32(hdr->seq_num) + 1; // A SYN consumes a single sequence number.
    conn->window_size = u16_from_net_u16(hdr->window_size);
    conn->state = TCP_CONN_STATE_SYN_RECEIVED;

    dlist_insert(&socket->conn_list_head, &conn->socket_conn_list);

    print_dbg(PINFO, STR("Created new connection after receiving SYN: %s\n"),
              tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));

    return tcp_send_segment(conn, TCP_HDR_FLAG_ACK | TCP_HDR_FLAG_SYN, sb, arn);
}

static struct result tcp_handle_receive_fin(struct ipv4_addr host_addr, struct ipv4_addr peer_addr,
                                            struct tcp_header *hdr, struct send_buf sb, struct arena arn)
{
    assert(hdr);

    u16 host_port = u16_from_net_u16(hdr->dest_port);
    u16 peer_port = u16_from_net_u16(hdr->src_port);

    struct tcp_socket *socket = tcp_lookup_socket(host_addr, host_port);
    if (!socket) {
        print_dbg(PDBG, STR("Received FIN for socket that doesn't exist (%s). Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    struct tcp_conn *conn = tcp_lookup_conn(socket, peer_addr, peer_port);
    if (!conn) {
        print_dbg(PDBG, STR("Received FIN from peer that's not connected (%s). Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    conn->ack_num = u32_from_net_u32(hdr->seq_num) + 1; // A FIN consumes a single sequence number.
    conn->state = TCP_CONN_STATE_CLOSING;

    // TODO: Don't got to state CLOSING right away. Instead, ACK the FIN that just came in an tell the application
    // that the stream has ended. Only send our own FIN when the application has closed the connection.

    print_dbg(PINFO, STR("Closing connection after receiving FIN: %s\n"),
              tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));

    return tcp_send_segment(conn, TCP_HDR_FLAG_ACK | TCP_HDR_FLAG_FIN, sb, arn);
}

static struct result tcp_handle_receive_segment(struct ipv4_addr host_addr, struct ipv4_addr peer_addr,
                                                struct tcp_header *hdr, struct byte_view payload, struct send_buf sb,
                                                struct arena arn)
{
    u16 host_port = u16_from_net_u16(hdr->dest_port);
    u16 peer_port = u16_from_net_u16(hdr->src_port);

    struct tcp_socket *socket = tcp_lookup_socket(host_addr, host_port);
    if (!socket) {
        print_dbg(PDBG, STR("Received TCP segment for socket that doesn't exist (%s). Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    struct tcp_conn *conn = tcp_lookup_conn(socket, peer_addr, peer_port);
    if (!conn) {
        print_dbg(PDBG, STR("Received TCP segment by peer without connection (%s). Dropping ...\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    if (conn->state == TCP_CONN_STATE_SYN_RECEIVED && u32_from_net_u32(hdr->ack_num) > conn->seq_num) {
        print_dbg(PINFO, STR("Moving connection %s to ESTABLISHED state\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        conn->state = TCP_CONN_STATE_ESTABLISHED;
    }

    if (conn->state == TCP_CONN_STATE_CLOSING && u32_from_net_u32(hdr->ack_num) > conn->seq_num) {
        print_dbg(PINFO, STR("Close of connection %s has been ACK'ed. Deleting the connection.\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        tcp_free_conn(conn);
        return result_ok();
    }

    if (conn->state != TCP_CONN_STATE_ESTABLISHED) {
        print_dbg(
            PDBG,
            STR("Received TCP segment by peer with a connection that's not in the ESTABLISHED state (%s). Dropping ...\n"),
            tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    conn->ack_num = u32_from_net_u32(hdr->seq_num) + payload.len;
    conn->seq_num = u32_from_net_u32(hdr->ack_num); // TODO: Update this to be the right sequence number.

    // TODO: Process data from the segment ...

    if (payload.len == 0) {
        print_dbg(PINFO, STR("Received TCP segment without payload. No need to respond (%s).\n"),
                  tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));
        return result_ok();
    }

    print_dbg(PINFO, STR("ACK'ing TCP segment ack_num=%u for %s\n"), conn->ack_num,
              tcp_fmt_conn(host_addr, peer_addr, host_port, peer_port, &arn));

    return tcp_send_segment(conn, TCP_HDR_FLAG_ACK, sb, arn);
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
        byte_view_new(segment.dat + sizeof(struct tcp_header), segment.len - sizeof(struct tcp_header));

    if (tcp_hdr->flags & TCP_HDR_FLAG_SYN) {
        // Respond with a passive open to an active open by a peer. We assume this is the only way to open a
        // connection for now.
        return tcp_handle_receive_syn(pseudo_hdr.dest_addr, pseudo_hdr.src_addr, tcp_hdr, sb, tmp);
    } else if (tcp_hdr->flags & TCP_HDR_FLAG_FIN) {
        return tcp_handle_receive_fin(pseudo_hdr.dest_addr, pseudo_hdr.src_addr, tcp_hdr, sb, tmp);
    } else {
        return tcp_handle_receive_segment(pseudo_hdr.dest_addr, pseudo_hdr.src_addr, tcp_hdr, payload, sb, tmp);
    }

    return result_ok();
}

///////////////////////////////////////////////////////////////////////////////
// User interface                                                            //
///////////////////////////////////////////////////////////////////////////////

struct tcp_conn *tcp_conn_listen_accept(struct ipv4_addr addr, u16 port, struct arena tmp)
{
    struct tcp_socket *socket = tcp_lookup_socket(addr, port);

    if (!socket) {
        socket = tcp_alloc_socket();
        if (!socket) {
            print_dbg(PERROR, STR("Failed to allocate new TCP socket (addr=%s, port=%hu).\n"),
                      ipv4_addr_format(addr, &tmp), port);
            return NULL;
        }

        socket->addr = addr;
        socket->port = port;
        socket->state = TCP_SOCKET_STATE_LISTENING;
        dlist_init_empty(&socket->conn_list_head);

        print_dbg(PINFO, STR("Listening for new connection on addr=%s port=%hu ...\n"), ipv4_addr_format(addr, &tmp),
                  port);

        return NULL;
    }

    if (dlist_is_empty(&socket->conn_list_head))
        return NULL;

    struct tcp_conn *conn = __container_of(socket->conn_list_head.next, struct tcp_conn, socket_conn_list);
    if (conn->state == TCP_CONN_STATE_ESTABLISHED)
        return conn;

    return NULL;
}

// Send `payload` to the other side of the connection `conn`.
struct result tcp_conn_send(struct tcp_conn *conn, struct byte_view payload, struct send_buf sb, struct arena tmp)
{
    (void)conn;
    (void)payload;
    (void)sb;
    (void)tmp;
    crash("We're not here yet!\n");
}

// Close the connection `*conn`. `conn` will be set to NULL since it's stale now.
struct result tcp_conn_close(struct tcp_conn **conn)
{
    (void)conn;
    crash("We're not here yet!\n");
}
