#include <tx/net/tcp.h>

#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/byte.h>
#include <tx/kvalloc.h>
#include <tx/list.h>
#include <tx/net/ip.h>
#include <tx/net/netorder.h>
#include <tx/pool.h>
#include <tx/print.h>

static bool global_tcp_is_initialized;

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

#define TCP_HDR_LEN_NO_OPT 5

#define TCP_HDR_FLAG_FIN BIT(0)
#define TCP_HDR_FLAG_SYN BIT(1)
#define TCP_HDR_FLAG_RST BIT(2)
#define TCP_HDR_FLAG_ACK BIT(4)

static_assert(sizeof(struct tcp_header) == 20);

#define TCP_OPT_EOL_KIND 0
#define TCP_OPT_NOP_KIND 1

// NOTE: The `TCP_OPT_*_LENGTH` constant does not always match the size of the corresponding `struct tcp_option_*`.
// That's because these structures are used only for constructing outgoing segments, not for parsing options in
// incoming segments. So these options can contain padding at the end that's not a proper part of the option. This
// leads to the length of the structure being greater than the length field.

struct tcp_option_mss {
    u8 kind;
    u8 length;
    net_u16 value;
} __packed;

static_assert(sizeof(struct tcp_option_mss) == 4);

#define TCP_OPT_MSS_KIND 2
#define TCP_OPT_MSS_LENGTH 4
#define TCP_OPT_MSS_VALUE 1460 /* Typical for ethernet with 1500 byte MTUs. */

struct tcp_option_ws {
    u8 kind;
    u8 length;
    u8 value;
    u8 nop; // Padding
} __packed;

static_assert(sizeof(struct tcp_option_ws) == 4);

#define TCP_OPT_WS_KIND 3
#define TCP_OPT_WS_LENGTH 3

struct tcp_option_ts {
    u8 kind;
    u8 length;
    net_u32 tsval;
    net_u32 tsecr;
    u8 nop1;
    u8 nop2;
} __packed;

static_assert(sizeof(struct tcp_option_ts) == 12);

#define TCP_OPT_TS_KIND 8
#define TCP_OPT_TS_LENGTH 10

///////////////////////////////////////////////////////////////////////////////
// Circular receive buffer                                                   //
///////////////////////////////////////////////////////////////////////////////

static sz global_tcp_stats_recv_mem;

struct recv_buf {
    struct byte_array data;
    sz head;
    sz tail;
};

static inline bool recv_buf_is_empty(struct recv_buf *buf)
{
    assert(buf);
    return buf->head == buf->tail;
}

static inline bool recv_buf_is_full(struct recv_buf *buf)
{
    assert(buf);
    return (buf->head + 1) % buf->data.len == buf->tail;
}

static inline sz recv_buf_count(struct recv_buf buf)
{
    assert(buf.data.len >= 0);
    if (buf.data.len == 0)
        return 0;
    return (buf.head - buf.tail + buf.data.len) % buf.data.len;
}

static inline sz recv_buf_space(struct recv_buf buf)
{
    if (buf.data.len == 0)
        return 0;
    return buf.data.len - 1 - recv_buf_count(buf);
}

static struct result recv_buf_alloc(struct recv_buf *buf, struct pool *alloc)
{
    assert(buf);
    assert(alloc);

    void *mem = pool_alloc(alloc);
    if (!mem)
        return result_error(ENOMEM);

    buf->data = byte_array_new(mem, alloc->size);
    buf->head = 0;
    buf->tail = 0;

    global_tcp_stats_recv_mem += alloc->size;

    return result_ok();
}

static void recv_buf_free(struct recv_buf *buf, struct pool *alloc)
{
    assert(buf);
    assert(alloc);

    if (buf->data.dat)
        pool_free(alloc, buf->data.dat);
    buf->data = byte_array_new(NULL, 0);

    global_tcp_stats_recv_mem -= alloc->size;
}

static struct result recv_buf_push_byte(struct recv_buf *buf, byte b)
{
    assert(buf);

    if (recv_buf_is_full(buf))
        return result_error(EAGAIN);

    buf->data.dat[buf->head] = b;
    buf->head = (buf->head + 1) % buf->data.len;
    return result_ok();
}

static struct result_byte recv_buf_pop_byte(struct recv_buf *buf)
{
    assert(buf);

    if (recv_buf_is_empty(buf))
        return result_byte_error(EAGAIN);

    byte b = buf->data.dat[buf->tail];
    buf->tail = (buf->tail + 1) % buf->data.len;
    return result_byte_ok(b);
}

static struct result recv_buf_write(struct recv_buf *buf, struct byte_view data)
{
    assert(buf);

    if (data.len > recv_buf_space(*buf))
        return result_error(EAGAIN);

    for (sz i = 0; i < data.len; i++) {
        struct result res = recv_buf_push_byte(buf, data.dat[i]);
        if (res.is_error)
            return res;
    }

    return result_ok();
}

static sz recv_buf_read(struct recv_buf *buf, struct byte_buf *dest)
{
    assert(buf);
    assert(dest);

    sz bytes_read = 0;
    sz available = recv_buf_count(*buf);
    sz space = dest->cap - dest->len;
    sz to_read = MIN(available, space);

    for (sz i = 0; i < to_read; i++) {
        struct result_byte res = recv_buf_pop_byte(buf);
        if (res.is_error)
            break;

        if (byte_buf_append_n(dest, 1, result_byte_checked(res)) != 1)
            break;

        bytes_read++;
    }

    return bytes_read;
}

///////////////////////////////////////////////////////////////////////////////
// Send buffer queue                                                         //
///////////////////////////////////////////////////////////////////////////////

#define TCP_SBQ_NUM 1024
#define TCP_SB_MAX_LEN 2048

static struct pool global_tcp_sbq_alloc;
static struct pool global_tcp_sb_alloc;

static sz global_tcp_stats_sbq_mem;

struct send_buf_queue {
    struct dlist link;
    struct send_buf sb;

    u32 seq_num;
    u32 required_ack;
    u8 flags;
    net_u16 checksum;
    sz len;

    sz n_transmissions;
    struct time_ms retry_after;
    struct time_ms last_try;
};

static struct send_buf_queue *tcp_alloc_sbq_and_sb(void)
{
    struct send_buf_queue *sbq = pool_alloc(&global_tcp_sbq_alloc);
    if (!sbq)
        return NULL;

    void *sb_mem = pool_alloc(&global_tcp_sb_alloc);
    if (!sb_mem) {
        pool_free(&global_tcp_sbq_alloc, sbq);
        return NULL;
    }

    sbq->sb = send_buf_new(arena_new(byte_array_new(sb_mem, TCP_SB_MAX_LEN)));

    global_tcp_stats_sbq_mem += global_tcp_sbq_alloc.size + global_tcp_sb_alloc.size;

    return sbq;
}

static void tcp_free_sbq_and_sb(struct send_buf_queue *sbq)
{
    assert(sbq);

    pool_free(&global_tcp_sb_alloc, sbq->sb.orig_arn.beg);
    pool_free(&global_tcp_sbq_alloc, sbq);

    global_tcp_stats_sbq_mem -= global_tcp_sbq_alloc.size + global_tcp_sb_alloc.size;
}

///////////////////////////////////////////////////////////////////////////////
// TCP connection                                                            //
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

#define TCP_CONN_DEFAULT_MSS 536 /* Based on RFC 9293 */
#define TCP_CONN_TIME_WAIT_MS 100 /* This is low so we can re-use connections quickly. */
#define TCP_CONN_RTO_MIN 200 /* Minimum RTO of 200ms */
#define TCP_CONN_RTO_MAX 30000 /* Maximum RTO of 30s */
#define TCP_CONN_RECV_BUF_SIZE 0x4000
#define TCP_CONN_DEFAULT_RECV_WINDOW_SIZE (TCP_CONN_RECV_BUF_SIZE / 2)

struct tsopt {
    bool have_ts;
    u32 tsval;
    u32 tsecr;
};

struct tcp_conn {
    bool is_used;
    struct ipv4_addr host_addr;
    struct ipv4_addr peer_addr;
    u16 host_port;
    u16 peer_port;
    enum tcp_conn_state state;
    sz mss; // Maximum Segment Size the peer is willing to receive.
    bool use_window_scale; // Should we send the window scale option?

    struct dlist accept_queue;

    // Transmission
    u32 send_unack; // SND.UNA
    u32 send_next; // SND.NXT
    sz send_window_real; // SND.WND (scale applied, can be bigger than a 16-bit unsigned integer).
    u8 send_window_scale;
    u32 iss; // Initial send sequence number (ISS).
    struct dlist send_queue;

    // Reception
    u32 recv_next; // RCV.NXT
    sz recv_window_size_real; // RCV.WND
    struct recv_buf recv_buf;

    // Set when the connection is put in the TIME_WAIT state. The connection is deleted when `TCP_CONN_TIME_WAIT_MS`
    // has passed (see `tcp_purge_old_conn` and calls sites of this function).
    struct time_ms time_wait_start;

    // Time stamp option state
    bool use_time_stamps;
    u32 ts_recent; // Most recent timestamp received.
    u32 last_ack_sent; // The sequence number sent in the last ACK segment.

    // Round-trip time and retransmission timeout
    u32 srtt; // Smooted RTT (in ms).
    u32 rttvar; // RTT variance (in ms).
    u32 rto; // Retranmission timeout (in ms).
};

static struct pool global_tcp_recv_buf_alloc;

// If we need to handle more connections at the same time, we could also allocate this array dynamically. The main
// reason for using an array is that it's simple to search (without requiring much pointer chasing like linked lists
// do).
#define TCP_CONN_MAX_NUM 64
static struct tcp_conn global_tcp_conn_table[TCP_CONN_MAX_NUM];

static void tcp_free_conn(struct tcp_conn *conn)
{
    assert(conn);

    recv_buf_free(&conn->recv_buf, &global_tcp_recv_buf_alloc);

    struct dlist *head = conn->send_queue.next;
    while (head != &conn->send_queue) {
        struct send_buf_queue *sbq = __container_of(head, struct send_buf_queue, link);
        struct dlist *next = head->next;
        dlist_remove(head);
        tcp_free_sbq_and_sb(sbq);
        head = next;
    }

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

static struct str tcp_conn_format_raw(struct ipv4_addr host_addr, struct ipv4_addr peer_addr, u16 host_port,
                                      u16 peer_port, struct arena *arn)
{
    assert(arn);
    struct str_buf sbuf = str_buf_from_byte_array(byte_array_from_arena(128, arn));
    assert(!fmt(&sbuf, STR("%s:%hu %s:%hu"), ipv4_addr_format(host_addr, arn), host_port,
                ipv4_addr_format(peer_addr, arn), peer_port)
                .is_error);
    return str_from_buf(sbuf);
}

struct str tcp_conn_format(struct tcp_conn *conn, struct arena *arn)
{
    assert(conn);
    return tcp_conn_format_raw(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, arn);
}

static struct result_u32 tcp_generate_isn(void)
{
    u64 isn_raw;
    bool success = rdrand_u64(&isn_raw);
    if (!success)
        return result_u32_error(EIO);
    return result_u32_ok((u32)isn_raw);
}

static struct tcp_conn *tcp_conn_alloc_and_init(struct ipv4_addr host_addr, u16 host_port, sz mss,
                                                enum tcp_conn_state state)
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

    conn->mss = mss;
    conn->use_window_scale = false;

    dlist_init_empty(&conn->accept_queue);

    conn->recv_next = 0;
    conn->recv_buf.data = byte_array_new(NULL, 0);
    conn->recv_buf.head = 0;
    conn->recv_buf.tail = 0;
    conn->recv_window_size_real = TCP_CONN_DEFAULT_RECV_WINDOW_SIZE;

    conn->iss = result_u32_checked(isn_res);
    conn->send_unack = conn->iss;
    conn->send_next = conn->iss;
    conn->send_window_real = 0;
    conn->send_window_scale = 0;

    dlist_init_empty(&conn->send_queue);

    conn->time_wait_start = time_ms_new(0);

    conn->use_time_stamps = false;
    conn->ts_recent = 0;
    conn->last_ack_sent = 0;

    conn->srtt = 0;
    conn->rttvar = 0;
    conn->rto = TCP_CONN_RTO_MIN;

    return conn;
}

static u32 tcp_get_timestamp_clock(void)
{
    return (u32)time_current_ms().ms;
}

static inline sz seq_num_relative(struct tcp_conn *conn, sz seq_num)
{
    return (u32)seq_num - (u32)conn->iss;
}

static inline bool seq_gt(u32 a, u32 b)
{
    return (i64)a - (i64)b > 0;
}

static inline bool seq_geq(u32 a, u32 b)
{
    return (i64)a - (i64)b >= 0;
}

static inline u32 abs_diff(u32 a, u32 b)
{
    return (a > b) ? (a - b) : (b - a);
}

static void tcp_conn_update_rtt(struct tcp_conn *conn, u32 rtt_sample)
{
    // This is according to RFC 6298.

    if (conn->srtt == 0) {
        conn->srtt = rtt_sample;
        conn->rttvar = rtt_sample / 2;
    } else {
        conn->rttvar = (3 * conn->rttvar + abs_diff(rtt_sample, conn->srtt)) / 4; // beta = 1/4
        conn->srtt = (7 * conn->srtt + rtt_sample) / 8; // alpha = 1/8
    }

    conn->rto = conn->srtt + MAX(50, 4 * conn->rttvar); // Minimum granularity of 50ms.
    conn->rto = MIN(TCP_CONN_RTO_MAX, MAX(TCP_CONN_RTO_MIN, conn->rto));
}

static void tcp_conn_update_send_state(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt)
{
    assert(conn);

    if (hdr->flags & TCP_HDR_FLAG_ACK) {
        u32 ack_num = u32_from_net_u32(hdr->ack_num);

        if (seq_gt(ack_num, conn->send_unack)) {
            conn->send_unack = ack_num;

            if (conn->use_time_stamps && tsopt.have_ts && tsopt.tsecr != 0) {
                u32 current_time = tcp_get_timestamp_clock();
                u32 rtt_sample = current_time - tsopt.tsecr;
                tcp_conn_update_rtt(conn, rtt_sample);
            }
        }
    }

    // NOTE: `conn->send_window_scale` is 0 if window scaling isn't used.
    conn->send_window_real = u16_from_net_u16(hdr->window_size) << conn->send_window_scale;
}

static sz tcp_conn_update_recv_state(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt,
                                     struct byte_view payload, struct arena tmp)
{
    assert(conn);
    assert(hdr);

    u32 seq_num = u32_from_net_u32(hdr->seq_num);

    if (conn->use_time_stamps && tsopt.have_ts && seq_num <= conn->last_ack_sent &&
        conn->last_ack_sent < seq_num + payload.len)
        conn->ts_recent = tsopt.tsval;

    if (payload.len > 0) {
        if (seq_num != conn->recv_next) {
            // We can support out-of-order delivery at a later time.
            print_dbg(PDBG, STR("Out-of-order segment received: expected seq=%u, got seq=%u (%s). Dropping ...\n"),
                      conn->recv_next, seq_num, tcp_conn_format(conn, &tmp));
            return 0;
        }

        struct result write_res = recv_buf_write(&conn->recv_buf, payload);
        if (write_res.is_error) {
            assert(write_res.code == EAGAIN);
            print_dbg(PWARN, STR("Not enough space in receive buffer to receive incoming segment (%s). Dropping ...\n"),
                      tcp_conn_format(conn, &tmp));
            return 0;
        }

        conn->recv_next += payload.len;
        conn->recv_window_size_real -= payload.len;
    }

    if (hdr->flags & TCP_HDR_FLAG_FIN) {
        if (seq_num + payload.len != conn->recv_next) {
            print_dbg(PDBG, STR("FIN received with unexpected sequence number (%s). Dropping ...\n"),
                      tcp_conn_format(conn, &tmp));
            return payload.len;
        }

        conn->recv_next++; // The incoming FIN consumed one sequence number.
        conn->recv_window_size_real--;
    }

    return payload.len;
}

static inline bool tcp_conn_needs_user_close(struct tcp_conn *conn)
{
    // NOTE: Once a connection enters the ESTABLISHED state, it becomes the user's responsibility to
    // close it. From the ESTABLISHED state, a connection can be moved to the CLOSE_WAIT state or the
    // RESET state without any user action (e.g., if a FIN is received). Thus, in any of these three
    // states, we need the user to call `close` on the connection to free it.
    return conn->state == TCP_CONN_STATE_ESTABLISHED || conn->state == TCP_CONN_STATE_CLOSE_WAIT ||
           conn->state == TCP_CONN_STATE_RESET;
}

///////////////////////////////////////////////////////////////////////////////
// TCP initialization                                                        //
///////////////////////////////////////////////////////////////////////////////

struct result tcp_init(void)
{
    assert(!global_tcp_is_initialized);

    struct option_byte_array recv_mem_opt = kvalloc_alloc(TCP_CONN_RECV_BUF_SIZE * TCP_CONN_MAX_NUM, alignof(void *));
    if (recv_mem_opt.is_none)
        return result_error(ENOMEM);
    struct byte_array recv_mem = option_byte_array_checked(recv_mem_opt);
    global_tcp_recv_buf_alloc = pool_new(recv_mem, TCP_CONN_RECV_BUF_SIZE);

    struct option_byte_array sbq_mem_opt =
        kvalloc_alloc(sizeof(struct send_buf_queue) * TCP_SBQ_NUM, alignof(struct send_buf_queue));
    if (sbq_mem_opt.is_none) {
        kvalloc_free(recv_mem);
        return result_error(ENOMEM);
    }
    struct byte_array sbq_mem = option_byte_array_checked(sbq_mem_opt);
    global_tcp_sbq_alloc = pool_new(sbq_mem, sizeof(struct send_buf_queue));

    struct option_byte_array sb_mem_opt = kvalloc_alloc(TCP_SB_MAX_LEN * TCP_SBQ_NUM, alignof(void *));
    if (sb_mem_opt.is_none) {
        kvalloc_free(recv_mem);
        kvalloc_free(sbq_mem);
        return result_error(ENOMEM);
    }
    struct byte_array sb_mem = option_byte_array_checked(sb_mem_opt);
    global_tcp_sb_alloc = pool_new(sb_mem, TCP_SB_MAX_LEN);

    global_tcp_is_initialized = true;
    return result_ok();
}

///////////////////////////////////////////////////////////////////////////////
// Transmit and retransmission logic                                         //
///////////////////////////////////////////////////////////////////////////////

static struct result tcp_send_segment_noqueue_raw(struct ipv4_addr host_addr, struct ipv4_addr peer_addr, u16 host_port,
                                                  u16 peer_port, u32 seq_num, u32 ack_num, sz window_size_real,
                                                  u8 window_scale, bool use_window_scale, u8 flags,
                                                  net_u16 payload_checksum, sz payload_len, u32 ts_recent,
                                                  bool use_time_stamps, struct send_buf sb, struct arena tmp)
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

    bool use_mss = flags & TCP_HDR_FLAG_SYN;
    bool use_ws = (flags & TCP_HDR_FLAG_SYN) && use_window_scale;
    bool use_ts = use_time_stamps;

    struct tcp_option_mss mss;
    if (use_mss) {
        mss.kind = TCP_OPT_MSS_KIND;
        mss.length = TCP_OPT_MSS_LENGTH;
        mss.value = net_u16_from_u16(TCP_OPT_MSS_VALUE);
    }

    struct tcp_option_ws ws;
    if (use_ws) {
        ws.kind = TCP_OPT_WS_KIND;
        ws.length = TCP_OPT_WS_LENGTH;
        ws.value = window_scale;
        ws.nop = TCP_OPT_NOP_KIND;
    }

    struct tcp_option_ts ts;
    if (use_ts) {
        ts.kind = TCP_OPT_TS_KIND;
        ts.length = TCP_OPT_TS_LENGTH;
        ts.tsval = net_u32_from_u32(tcp_get_timestamp_clock());
        ts.tsecr = net_u32_from_u32(ts_recent);
        ts.nop1 = TCP_OPT_NOP_KIND;
        ts.nop2 = TCP_OPT_NOP_KIND;
    }

    sz opt_size = (use_mss ? sizeof(mss) : 0) + (use_ws ? sizeof(ws) : 0) + (use_ts ? sizeof(ts) : 0);
    assert(IS_ALIGNED(opt_size, 4)); // `opt_size` must be evenly divisible by 4.

    struct tcp_header hdr;
    hdr.src_port = net_u16_from_u16(host_port);
    hdr.dest_port = net_u16_from_u16(peer_port);
    hdr.seq_num = net_u32_from_u32(seq_num);
    hdr.ack_num = net_u32_from_u32(ack_num);
    hdr.header_len = TCP_HDR_LEN_NO_OPT + (opt_size / 4);
    hdr.reserved = 0;
    hdr.flags = flags;
    hdr.window_size = net_u16_from_u16(window_size_real >> window_scale);
    hdr.checksum = net_u16_from_u16(0);
    hdr.urgent = net_u16_from_u16(0);

    struct tcp_ip_pseudo_header pseudo_hdr;
    pseudo_hdr.src_addr = interface_addr;
    pseudo_hdr.dest_addr = peer_addr;
    pseudo_hdr.zero = 0;
    pseudo_hdr.protocol = IPV4_PROTOCOL_TCP;
    pseudo_hdr.tcp_length = net_u16_from_u16(sizeof(hdr) + opt_size + payload_len);

    net_u16 checksum = net_u16_from_u16(0);
    checksum = internet_checksum_add(checksum, payload_checksum);
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&hdr, sizeof(hdr)));
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&pseudo_hdr, sizeof(pseudo_hdr)));
    if (use_mss)
        checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&mss, sizeof(mss)));
    if (use_ws)
        checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&ws, sizeof(ws)));
    if (use_ts)
        checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&ts, sizeof(ts)));
    hdr.checksum = internet_checksum_finalize(checksum);

    struct byte_buf *buf = NULL;

    if (opt_size) {
        buf = send_buf_prepend(&sb, opt_size);
        if (!buf)
            return result_error(ENOMEM);
        if (use_mss)
            assert(byte_buf_append(buf, byte_view_new((void *)&mss, sizeof(mss))) == sizeof(mss));
        if (use_ws)
            assert(byte_buf_append(buf, byte_view_new((void *)&ws, sizeof(ws))) == sizeof(ws));
        if (use_ts)
            assert(byte_buf_append(buf, byte_view_new((void *)&ts, sizeof(ts))) == sizeof(ts));
    }

    buf = send_buf_prepend(&sb, sizeof(hdr));
    if (!buf)
        return result_error(ENOMEM);
    assert(byte_buf_append(buf, byte_view_new((void *)&hdr, sizeof(hdr))) == sizeof(hdr));

    return ipv4_send_packet(peer_addr, IPV4_PROTOCOL_TCP, sb, tmp);
}

static struct result tcp_send_segment_noqueue(struct tcp_conn *conn, u8 flags, u32 seq_num, net_u16 payload_checksum,
                                              sz payload_len, struct send_buf sb, struct arena arn)
{
    assert(conn);

    if (conn->state != TCP_CONN_STATE_LISTEN && conn->state != TCP_CONN_STATE_SYN_RCVD) {
        sz space = recv_buf_space(conn->recv_buf);
        if (space - conn->recv_window_size_real >= TCP_OPT_MSS_VALUE)
            conn->recv_window_size_real = space;
    }

    if (flags & TCP_HDR_FLAG_ACK)
        conn->last_ack_sent = conn->recv_next;

    return tcp_send_segment_noqueue_raw(conn->host_addr, conn->peer_addr, conn->host_port, conn->peer_port, seq_num,
                                        conn->recv_next, conn->recv_window_size_real, 0, conn->use_window_scale, flags,
                                        payload_checksum, payload_len, conn->ts_recent, conn->use_time_stamps, sb, arn);
}

static struct result tcp_poll_retransmit_conn(struct tcp_conn *conn, struct arena tmp)
{
    struct dlist *head = conn->send_queue.next;

    while (head != &conn->send_queue) {
        struct send_buf_queue *sbq = __container_of(head, struct send_buf_queue, link);

        // We can remove the buffer from the retransmission queue if the cummulative ACK numbers we received
        // are greater than the last ACK number required by the buffer. Alternatively, we give up after a few retries.
        if (seq_geq(conn->send_unack, sbq->required_ack) || sbq->n_transmissions > 8) {
            print_dbg(PVERBOSE, STR("Freeing sbq 0x%lx seq_num=%ld\n"), &sbq, seq_num_relative(conn, sbq->seq_num));
            struct dlist *next = head->next;
            dlist_remove(head);
            head = next;
            tcp_free_sbq_and_sb(sbq);
            continue;
        }

        struct time_ms now = time_current_ms();
        if (now.ms >= sbq->last_try.ms + sbq->retry_after.ms) {
            print_dbg(
                PVERBOSE,
                STR("(%s) Retransmitting datagram seq_num=%ld required_ack=%u n_transmissions=%ld now=%lu last_try=%lu retry_after=%lu\n"),
                tcp_conn_format(conn, &tmp), seq_num_relative(conn, sbq->seq_num),
                seq_num_relative(conn, sbq->required_ack), sbq->n_transmissions, now.ms, sbq->last_try.ms,
                sbq->retry_after.ms);
            struct result res =
                tcp_send_segment_noqueue(conn, sbq->flags, sbq->seq_num, sbq->checksum, sbq->len, sbq->sb, tmp);
            if (res.is_error)
                return res;
            sbq->n_transmissions++;
            sbq->retry_after = time_ms_new(sbq->retry_after.ms * 2);
            sbq->last_try = now;
        }

        return result_ok();
    }

    return result_ok();
}

struct result tcp_poll_retransmit(struct arena tmp)
{
    assert(global_tcp_is_initialized);

    struct result res = result_ok();

    for (sz i = 0; i < TCP_CONN_MAX_NUM; i++) {
        if (global_tcp_conn_table[i].is_used) {
            res = tcp_poll_retransmit_conn(&global_tcp_conn_table[i], tmp);
            if (res.is_error)
                return res;
        }
    }

    return res;
}

static struct result tcp_send_segment(struct tcp_conn *conn, u8 flags, struct byte_view fragment, struct arena tmp)
{
    assert(conn);

    u32 seq_num = conn->send_next;

    conn->send_next += fragment.len;
    if (flags & TCP_HDR_FLAG_SYN)
        conn->send_next++;
    if (flags & TCP_HDR_FLAG_FIN)
        conn->send_next++;

    struct send_buf_queue *sbq = tcp_alloc_sbq_and_sb();
    if (!sbq)
        return result_error(ENOMEM);

    sbq->seq_num = seq_num;
    sbq->required_ack = conn->send_next;
    sbq->flags = flags;
    sbq->last_try = time_current_ms();
    sbq->retry_after = time_ms_new(conn->rto);
    sbq->n_transmissions = 1;
    sbq->checksum = internet_checksum_iterate(net_u16_from_u16(0), fragment);
    sbq->len = fragment.len;

    if (fragment.len) {
        struct byte_buf *buf = send_buf_prepend(&sbq->sb, fragment.len);
        assert(byte_buf_append(buf, fragment) == fragment.len);
    }

    dlist_insert(conn->send_queue.prev, &sbq->link);

    return tcp_send_segment_noqueue(conn, sbq->flags, sbq->seq_num, sbq->checksum, sbq->len, sbq->sb, tmp);
}

static inline struct result tcp_send_segment_empty(struct tcp_conn *conn, u8 flags, struct arena arn)
{
    return tcp_send_segment(conn, flags, byte_view_new(NULL, 0), arn);
}

///////////////////////////////////////////////////////////////////////////////
// Handling incoming segments and manage the TCP state machine               //
///////////////////////////////////////////////////////////////////////////////

static struct result tcp_handle_receive_listen(struct tcp_conn *listen_conn, struct ipv4_addr peer_addr, u16 peer_port,
                                               struct tcp_header *hdr, struct tsopt tsopt, struct arena tmp)
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

    struct tcp_conn *conn = tcp_conn_alloc_and_init(listen_conn->host_addr, listen_conn->host_port, listen_conn->mss,
                                                    TCP_CONN_STATE_SYN_RCVD);
    if (!conn) {
        print_dbg(PDBG, STR("Failed to allocate and initialize new SYN_RCVD TCP connection (%s).\n"),
                  tcp_conn_format_raw(listen_conn->host_addr, peer_addr, listen_conn->host_port, peer_port, &tmp));
        return result_error(ENOMEM);
    }

    dlist_insert(&listen_conn->accept_queue, &conn->accept_queue);

    conn->peer_addr = peer_addr;
    conn->peer_port = peer_port;
    // The SYN in the incoming header has consumed one sequence number so we add one to the ISN send by our peer.
    conn->recv_next = u32_from_net_u32(hdr->seq_num) + 1;
    conn->recv_window_size_real--;
    conn->send_window_scale = listen_conn->send_window_scale;
    conn->use_window_scale = listen_conn->use_window_scale;

    if (tsopt.have_ts) {
        conn->use_time_stamps = true;
        conn->ts_recent = tsopt.tsval;
    }

    print_dbg(
        PDBG,
        STR("Received SYN for a connection in the LISTEN state (%s). Responding with SYN + ACK. Created a new connection in the SYN_RCVD state.\n"),
        tcp_conn_format(conn, &tmp));

    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_SYN | TCP_HDR_FLAG_ACK, tmp);
}

static struct result tcp_handle_receive_syn_rcvd(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt,
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
    tcp_conn_update_send_state(conn, hdr, tsopt);

    // We start receiving data in the ESTABLISHED state so we need to allocate a buffer at this point.
    struct result buf_alloc_res = recv_buf_alloc(&conn->recv_buf, &global_tcp_recv_buf_alloc);
    if (buf_alloc_res.is_error) {
        print_dbg(
            PWARN,
            STR("Failed to allocate receive buffer for a connection (%s). Resetting and deleting the connection.\n"),
            tcp_conn_format(conn, &tmp));
        tcp_send_segment_empty(conn, TCP_HDR_FLAG_RST, tmp);
        tcp_free_conn(conn);
        return result_error(ENOMEM);
    }

    print_dbg(
        PDBG,
        STR("Received ACK for a connection in the SYN_RCVD state (%s). Not responding. The connection is ESTABLISHED now.\n"),
        tcp_conn_format(conn, &tmp));

    return result_ok();
}

static struct result tcp_handle_receive_established(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt,
                                                    struct byte_view payload, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_ESTABLISHED);

    tcp_conn_update_send_state(conn, hdr, tsopt);
    sz n_received = tcp_conn_update_recv_state(conn, hdr, tsopt, payload, tmp);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        conn->state = TCP_CONN_STATE_RESET;

        print_dbg(
            PDBG,
            STR("Received RST for a connection in the ESTABLISHED state (%s). Not responding. The connection is in the RESET state now.\n"),
            tcp_conn_format(conn, &tmp));

        return result_ok();
    }

    if (hdr->flags & TCP_HDR_FLAG_FIN) {
        conn->state = TCP_CONN_STATE_CLOSE_WAIT;

        print_dbg(
            PDBG,
            STR("Received FIN for a connection in the ESTABLISHED state (%s). Responding with ACK. The connection is in the CLOSE_WAIT state now.\n"),
            tcp_conn_format(conn, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, tmp);
    }

    if (n_received > 0) {
        print_dbg(PDBG, STR("Received %ld bytes of data for connection %s. Responding with ACK.\n"), n_received,
                  tcp_conn_format(conn, &tmp));
        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, tmp);
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
            STR("Received an ACK or RST (flags=%hhu) for a connection in the LAST_ACK state (%s). Not responding. The connection is deleted now.\n"),
            hdr->flags, tcp_conn_format(conn, &tmp));

        tcp_free_conn(conn);
    }
}

static struct result tcp_handle_receive_fin_wait_1(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt,
                                                   struct byte_view payload, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_FIN_WAIT_1);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the FIN_WAIT_1 state (%s). Not responding. The connection is deleted now.\n"),
            tcp_conn_format(conn, &tmp));
        tcp_free_conn(conn);
        return result_ok();
    }

    if ((hdr->flags & TCP_HDR_FLAG_FIN) && (hdr->flags & TCP_HDR_FLAG_ACK)) {
        conn->state = TCP_CONN_STATE_TIME_WAIT;
        conn->time_wait_start = time_current_ms();
        tcp_conn_update_send_state(conn, hdr, tsopt);
        tcp_conn_update_recv_state(conn, hdr, tsopt, payload, tmp);

        print_dbg(
            PDBG,
            STR("Received FIN + ACK for a connection in the FIN_WAIT_1 state (%s). Responding with ACK. The connection is in the TIME_WAIT state now.\n"),
            tcp_conn_format(conn, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, tmp);
    } else if (hdr->flags & TCP_HDR_FLAG_FIN) {
        conn->state = TCP_CONN_STATE_CLOSING;
        tcp_conn_update_send_state(conn, hdr, tsopt);
        tcp_conn_update_recv_state(conn, hdr, tsopt, payload, tmp);

        print_dbg(
            PDBG,
            STR("Received FIN for a connection in the FIN_WAIT_1 state (%s). Responding with ACK. The connection is in the CLOSING state now.\n"),
            tcp_conn_format(conn, &tmp));

        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, tmp);
    } else if (hdr->flags & TCP_HDR_FLAG_ACK) {
        conn->state = TCP_CONN_STATE_FIN_WAIT_2;
        tcp_conn_update_send_state(conn, hdr, tsopt);
        tcp_conn_update_recv_state(conn, hdr, tsopt, payload, tmp);

        print_dbg(
            PDBG,
            STR("Received ACK for a connection in the FIN_WAIT_1 state (%s). Not responding. The connection is in the FIN_WAIT_2 state now.\n"),
            tcp_conn_format(conn, &tmp));

        return result_ok();
    }

    // Stay in the FIN_WAIT_1 state if neither an ACK, nor a FIN, nor both were received.
    return result_ok();
}

static struct result tcp_handle_receive_fin_wait_2(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt,
                                                   struct byte_view payload, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_FIN_WAIT_2);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the FIN_WAIT_2 state (%s). Not responding. The connection is deleted now.\n"),
            tcp_conn_format(conn, &tmp));
        tcp_free_conn(conn);
        return result_ok();
    }

    // The connection is half open in the FIN_WAIT_2 state. The state of the connection must be updated
    // so remaining data is retransmitted correctly.
    tcp_conn_update_send_state(conn, hdr, tsopt);
    tcp_conn_update_recv_state(conn, hdr, tsopt, payload, tmp);

    if (!(hdr->flags & TCP_HDR_FLAG_FIN))
        return result_ok();

    conn->state = TCP_CONN_STATE_TIME_WAIT;
    conn->time_wait_start = time_current_ms();

    print_dbg(
        PDBG,
        STR("Received FIN for a connection in the FIN_WAIT_2 state (%s). Responding with ACK. The connection is in the TIME_WAIT state now.\n"),
        tcp_conn_format(conn, &tmp));

    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, tmp);
}

static void tcp_handle_receive_closing(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt,
                                       struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_CLOSING);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the CLOSING state (%s). Not responding. The connection is deleted now.\n"),
            tcp_conn_format(conn, &tmp));
        tcp_free_conn(conn);
        return;
    }

    if (!(hdr->flags & TCP_HDR_FLAG_ACK))
        return;

    conn->state = TCP_CONN_STATE_TIME_WAIT;
    conn->time_wait_start = time_current_ms();
    tcp_conn_update_send_state(conn, hdr, tsopt);

    print_dbg(
        PDBG,
        STR("Received ACK for a connection in the CLOSING state (%s). Not responding. The connection is in the TIME_WAIT state now.\n"),
        tcp_conn_format(conn, &tmp));
}

static struct result tcp_handle_receive_time_wait(struct tcp_conn *conn, struct tcp_header *hdr, struct tsopt tsopt,
                                                  struct byte_view payload, struct arena tmp)
{
    assert(conn);
    assert(hdr);
    assert(conn->state == TCP_CONN_STATE_TIME_WAIT);

    if (hdr->flags & TCP_HDR_FLAG_RST) {
        print_dbg(
            PDBG,
            STR("Received RST for a connection in the TIME_WAIT state (%s). Not responding. The connection is deleted now.\n"),
            tcp_conn_format(conn, &tmp));
        tcp_free_conn(conn);
        return result_ok();
    }

    // No user will ever see that data that we receive here. The only purpose of updating the send and receive states
    // at this point is to make sure that we ACK everything that the peer sent us when returning from this function.
    tcp_conn_update_send_state(conn, hdr, tsopt);
    tcp_conn_update_recv_state(conn, hdr, tsopt, payload, tmp);

    if (!(hdr->flags & TCP_HDR_FLAG_FIN))
        return result_ok();

    print_dbg(
        PDBG,
        STR("Received FIN for a connection in the TIME_WAIT state (%s). Responding with ACK. The connection remains in the TIME_WAIT state.\n"),
        tcp_conn_format(conn, &tmp));

    return tcp_send_segment_empty(conn, TCP_HDR_FLAG_ACK, tmp);
}

static bool tcp_checksum_is_ok(struct tcp_ip_pseudo_header pseudo_hdr, struct byte_view segment)
{
    net_u16 checksum = net_u16_from_u16(0);
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&pseudo_hdr, sizeof(pseudo_hdr)));
    checksum = internet_checksum_iterate(checksum, segment);
    return internet_checksum_finalize(checksum).inner == 0;
}

static struct tsopt tcp_handle_options(struct tcp_conn *conn, u8 flags, struct byte_view opts)
{
    struct tsopt tsopt;
    tsopt.have_ts = false;
    tsopt.tsval = 0;
    tsopt.tsecr = 0;

    sz i = 0;
    while (i < opts.len) {
        switch (opts.dat[i]) {
        case TCP_OPT_EOL_KIND:
            return tsopt;
        case TCP_OPT_NOP_KIND:
            i++;
            break;
        case TCP_OPT_MSS_KIND:
            // We can ignore the length field because it's always 4.
            if (i + TCP_OPT_MSS_LENGTH > opts.len)
                return tsopt;
            conn->mss = MAX(((u16)opts.dat[i + 2] << 8) | (u16)opts.dat[i + 3], TCP_CONN_DEFAULT_MSS);
            i += TCP_OPT_MSS_LENGTH;
            break;
        case TCP_OPT_WS_KIND:
            if (i + TCP_OPT_WS_LENGTH > opts.len)
                return tsopt;
            if ((flags & TCP_HDR_FLAG_SYN) && opts.dat[i + 2] <= 14) {
                conn->send_window_scale = opts.dat[i + 2];
                conn->use_window_scale = true;
            }
            i += TCP_OPT_WS_LENGTH;
            break;
        case TCP_OPT_TS_KIND:
            if (i + TCP_OPT_TS_LENGTH > opts.len)
                return tsopt;
            tsopt.have_ts = true;
            tsopt.tsval = ((u32)opts.dat[i + 2] << 24) | ((u32)opts.dat[i + 3] << 16) | ((u32)opts.dat[i + 4] << 8) |
                          (u32)opts.dat[i + 5];
            tsopt.tsecr = ((u32)opts.dat[i + 6] << 24) | ((u32)opts.dat[i + 7] << 16) | ((u32)opts.dat[i + 8] << 8) |
                          (u32)opts.dat[i + 9];
            i += TCP_OPT_TS_LENGTH;
            break;
        default:
            // All options except for EOL and NOP have a "length" field after the "kind" field. We use the "length"
            // field to skip the rest of this option.
            if (i + 2 > opts.len)
                return tsopt;
            i += opts.dat[i + 1];
            break;
        }
    }

    return tsopt;
}

struct result tcp_handle_packet(struct tcp_ip_pseudo_header pseudo_hdr, struct byte_view segment, struct send_buf sb,
                                struct arena tmp)
{
    assert(global_tcp_is_initialized);

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

    if (tcp_hdr->header_len < TCP_HDR_LEN_NO_OPT) {
        print_dbg(PDBG,
                  STR("Received TCP segment with invalid header length %hhd (must be at least " TOSTRING(
                      TCP_HDR_LEN_NO_OPT) "). Dropping ...\n"),
                  tcp_hdr->header_len);
        return result_ok();
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
                  tcp_conn_format_raw(host_addr, peer_addr, host_port, peer_port, &tmp));
        return tcp_send_segment_noqueue_raw(host_addr, peer_addr, host_port, peer_port,
                                            u32_from_net_u32(tcp_hdr->ack_num), u32_from_net_u32(tcp_hdr->seq_num),
                                            TCP_CONN_DEFAULT_RECV_WINDOW_SIZE, 0, false, TCP_HDR_FLAG_RST,
                                            net_u16_from_u16(0), 0, 0, false, sb, tmp);
    }

    struct tsopt tsopt;
    tsopt.have_ts = false;
    tsopt.tsval = 0;
    tsopt.tsecr = 0;

    if (tcp_hdr->header_len > TCP_HDR_LEN_NO_OPT) {
        struct byte_view tcp_options =
            byte_view_new(segment.dat + TCP_HDR_LEN_NO_OPT * 4, (tcp_hdr->header_len - TCP_HDR_LEN_NO_OPT) * 4);
        tsopt = tcp_handle_options(conn, tcp_hdr->flags, tcp_options);
    }

    switch (conn->state) {
    case TCP_CONN_STATE_LISTEN:
        return tcp_handle_receive_listen(conn, peer_addr, peer_port, tcp_hdr, tsopt, tmp);
    case TCP_CONN_STATE_SYN_RCVD:
        return tcp_handle_receive_syn_rcvd(conn, tcp_hdr, tsopt, tmp);
    case TCP_CONN_STATE_ESTABLISHED:
        return tcp_handle_receive_established(conn, tcp_hdr, tsopt, payload, tmp);
    case TCP_CONN_STATE_CLOSE_WAIT:
        return result_ok(); // We are just waiting for the user to close the connection. There is nothing to do.
    case TCP_CONN_STATE_LAST_ACK:
        tcp_handle_receive_last_ack(conn, tcp_hdr, tmp);
        return result_ok();
    case TCP_CONN_STATE_FIN_WAIT_1:
        return tcp_handle_receive_fin_wait_1(conn, tcp_hdr, tsopt, payload, tmp);
    case TCP_CONN_STATE_FIN_WAIT_2:
        return tcp_handle_receive_fin_wait_2(conn, tcp_hdr, tsopt, payload, tmp);
    case TCP_CONN_STATE_CLOSING:
        tcp_handle_receive_closing(conn, tcp_hdr, tsopt, tmp);
        return result_ok();
    case TCP_CONN_STATE_TIME_WAIT:
        return tcp_handle_receive_time_wait(conn, tcp_hdr, tsopt, payload, tmp);
    case TCP_CONN_STATE_RESET:
        return result_ok(); // We are just waiting for the user to close the connection. There is nothing to do.
    default:
        print_dbg(PERROR, STR("Unknown connection state %d for %s.\n"), conn->state,
                  tcp_conn_format_raw(host_addr, peer_addr, host_port, peer_port, &tmp));
        crash("Connection state invalid");
    }
}

///////////////////////////////////////////////////////////////////////////////
// User interface                                                            //
///////////////////////////////////////////////////////////////////////////////

static sz global_tcp_stats_bytes_tx;
static sz global_tcp_stats_bytes_rx;

struct tcp_conn *tcp_conn_listen(struct ipv4_addr addr, u16 port, struct arena tmp)
{
    assert(global_tcp_is_initialized);

    struct tcp_conn *conn = tcp_lookup_conn(addr, ipv4_addr_new(0, 0, 0, 0), port, 0, true);

    if (conn && conn->state == TCP_CONN_STATE_LISTEN)
        return conn;

    conn = tcp_conn_alloc_and_init(addr, port, TCP_CONN_DEFAULT_MSS, TCP_CONN_STATE_LISTEN);
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
    assert(global_tcp_is_initialized);
    assert(listen_conn);

    struct tcp_conn *conn = __container_of(listen_conn->accept_queue.next, struct tcp_conn, accept_queue);
    assert(conn);

    if (conn == listen_conn)
        return NULL;

    // Depending on the timing, the user may call accept when a SYN has been received but before the handshake was
    // completed. In that case the connection is in the SYN_RCVD state, but it's not ready to receive data.
    if (!tcp_conn_needs_user_close(conn))
        return NULL;

    dlist_remove(&conn->accept_queue);

    return conn;
}

static inline bool tcp_conn_closed_by_peer(enum tcp_conn_state state)
{
    return (state == TCP_CONN_STATE_CLOSE_WAIT) || (state == TCP_CONN_STATE_RESET);
}

static inline sz tcp_send_window_avail(struct tcp_conn *conn)
{
    sz send_next = conn->send_next;
    sz send_unack = conn->send_unack;

    // This is only possible if the window size got decreased so that `send_next` now lies beyond the right edge
    // of the send window. We can't send any more data until ACKs have arrived and `send_unack` has advanced.
    if (send_next > send_unack + conn->send_window_real)
        return 0;
    return (send_unack + conn->send_window_real) - send_next;
}

struct result_sz tcp_conn_send(struct tcp_conn *conn, struct byte_view payload, bool *peer_closed_conn,
                               struct arena tmp)
{
    assert(global_tcp_is_initialized);
    assert(conn);
    assert(peer_closed_conn);

    *peer_closed_conn = tcp_conn_closed_by_peer(conn->state);

    struct result_sz ip_mtu_res = ipv4_route_mtu(conn->peer_addr);
    if (ip_mtu_res.is_error)
        return result_sz_error(ip_mtu_res.code);

    sz ip_mtu = result_sz_checked(ip_mtu_res);
    sz len = MAX(0, ip_mtu - sizeof(struct tcp_header));
    len = MIN(len, conn->mss);
    len = MIN(len, payload.len);

    struct result res = result_ok();
    sz n_sent = 0;

    for (sz i = 0; i < payload.len; i += len) {
        if (tcp_send_window_avail(conn) < MIN(payload.len - i, len))
            return result_sz_ok(n_sent);

        struct byte_view fragment = byte_view_new(payload.dat + i, MIN(payload.len - i, len));
        res = tcp_send_segment(conn, TCP_HDR_FLAG_ACK, fragment, tmp);
        if (res.is_error)
            return result_sz_error(res.code);

        n_sent += fragment.len;
        global_tcp_stats_bytes_tx += fragment.len;
    }

    return result_sz_ok(n_sent);
}

struct result_sz tcp_conn_recv(struct tcp_conn *conn, struct byte_buf *buf, bool *peer_closed_conn)
{
    assert(global_tcp_is_initialized);
    assert(conn);
    assert(buf);
    assert(peer_closed_conn);

    *peer_closed_conn = tcp_conn_closed_by_peer(conn->state);

    sz avail = recv_buf_count(conn->recv_buf);
    if (!avail)
        return result_sz_ok(0);

    recv_buf_read(&conn->recv_buf, buf);

    global_tcp_stats_bytes_rx += avail;

    return result_sz_ok(avail);
}

struct result tcp_conn_close(struct tcp_conn **conn_ptr, struct arena tmp)
{
    assert(global_tcp_is_initialized);
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
        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_FIN | TCP_HDR_FLAG_ACK, tmp);
    }

    if (conn->state == TCP_CONN_STATE_CLOSE_WAIT) {
        conn->state = TCP_CONN_STATE_LAST_ACK;

        // The user has now lost access to this connection. We are only waiting to receive an ACK from the peer for
        // this FIN and then the connection will be deleted.
        return tcp_send_segment_empty(conn, TCP_HDR_FLAG_FIN | TCP_HDR_FLAG_ACK, tmp);
    }

    // All other states mean that a close operation is already in progress so we don't need to act.

    return result_ok();
}

///////////////////////////////////////////////////////////////////////////////
// TCP statistics                                                            //
///////////////////////////////////////////////////////////////////////////////

static sz tcp_stats_count_conns(void)
{
    sz n_conns = 0;

    for (sz i = 0; i < TCP_CONN_MAX_NUM; i++) {
        struct tcp_conn *conn = &global_tcp_conn_table[i];

        if (conn->is_used)
            n_conns++;
    }

    return n_conns;
}

struct tcp_stats tcp_stats_get(void)
{
    struct tcp_stats stats;
    stats.uptime = time_current_ms();
    stats.n_connections = tcp_stats_count_conns();
    stats.sbq_mem = global_tcp_stats_sbq_mem;
    stats.recv_mem = global_tcp_stats_recv_mem;
    stats.bytes_tx = global_tcp_stats_bytes_tx;
    stats.bytes_rx = global_tcp_stats_bytes_rx;
    return stats;
}
