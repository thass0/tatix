#include <tx/net/tcp.h>

#include <tx/assert.h>
#include <tx/byte.h>
#include <tx/kvalloc.h>
#include <tx/net/ip.h>
#include <tx/net/netorder.h>
#include <tx/pool.h>
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

#define TCP_HDR_FLAG_SYN BIT(1)
#define TCP_HDR_FLAG_ACK BIT(4)

static_assert(sizeof(struct tcp_header) == 20);

///////////////////////////////////////////////////////////////////////////////
// Manage connections                                                        //
///////////////////////////////////////////////////////////////////////////////

enum tcp_conn_state {
    TCP_CONN_CLOSED,
    TCP_CONN_STATE_SYN_RECEIVED,
    TCP_CONN_STATE_ESTABLISHED,
};

struct tcp_conn {
    u16 host_port;
    u16 peer_port;
    u16 host_addr;
    u16 peer_addr;
    u32 seq_num;
    u32 ack_sum;
    u16 window_size;
    enum tcp_conn_state state;
};

#define TCP_CONN_MAX_NUM 64
static struct pool global_tcp_conn_pool;
static bool global_tcp_initialized;

void tcp_init(void)
{
    assert(!global_tcp_initialized);
    struct byte_array mem =
        option_byte_array_checked(kvalloc_alloc(TCP_CONN_MAX_NUM * sizeof(struct tcp_conn), alignof(struct tcp_conn)));
    global_tcp_conn_pool = pool_new(mem, TCP_CONN_MAX_NUM);
    global_tcp_initialized = true;
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

static struct result tcp_handle_receive_syn(struct ipv4_addr src_addr, struct tcp_header *hdr, struct send_buf sb,
                                            struct arena tmp)
{
    assert(hdr);

    struct result_ipv4_addr interface_ip_res = ipv4_route_interface_addr(src_addr);
    if (interface_ip_res.is_error)
        return result_error(interface_ip_res.code);
    struct ipv4_addr interface_ip = result_ipv4_addr_checked(interface_ip_res);

    struct tcp_header out_hdr;
    out_hdr.src_port = hdr->dest_port;
    out_hdr.dest_port = hdr->src_port;
    out_hdr.seq_num = net_u32_from_u32(42);
    out_hdr.ack_num = net_u32_from_u32(u32_from_net_u32(hdr->seq_num) + 1);
    out_hdr.header_len = TCP_HEADER_LEN_NO_OPT;
    out_hdr.reserved = 0;
    out_hdr.flags = TCP_HDR_FLAG_SYN | TCP_HDR_FLAG_ACK;
    out_hdr.window_size = hdr->window_size;
    out_hdr.checksum = net_u16_from_u16(0);
    out_hdr.urgent = net_u16_from_u16(0);

    struct tcp_ip_pseudo_header pseudo_hdr;
    pseudo_hdr.src_addr = interface_ip;
    pseudo_hdr.dest_addr = src_addr;
    pseudo_hdr.zero = 0;
    pseudo_hdr.protocol = IPV4_PROTOCOL_TCP;
    pseudo_hdr.tcp_length = net_u16_from_u16(sizeof(out_hdr));

    net_u16 checksum = net_u16_from_u16(0);
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&out_hdr, sizeof(out_hdr)));
    checksum = internet_checksum_iterate(checksum, byte_view_new((void *)&pseudo_hdr, sizeof(pseudo_hdr)));
    out_hdr.checksum = internet_checksum_finalize(checksum);
    assert(tcp_checksum_is_ok(pseudo_hdr, byte_view_new((void *)&out_hdr, sizeof(out_hdr))));

    struct byte_buf *buf = send_buf_prepend(&sb, sizeof(out_hdr));
    if (!buf)
        return result_error(ENOMEM);

    assert(byte_buf_append(buf, byte_view_new((void *)&out_hdr, sizeof(out_hdr))) == sizeof(out_hdr));

    return ipv4_send_packet(src_addr, IPV4_PROTOCOL_TCP, sb, tmp);
}

struct result tcp_handle_packet(struct tcp_ip_pseudo_header pseudo_hdr, struct byte_view segment, struct send_buf sb,
                                struct arena tmp)
{
    assert(global_tcp_initialized);

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

    print_dbg(PINFO, STR("TCP header: header_len=%hhd flags=0x%hhx\n"), tcp_hdr->header_len, tcp_hdr->flags);

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

    struct byte_view data =
        byte_view_new(segment.dat + sizeof(struct tcp_header), segment.len - sizeof(struct tcp_header));

    if (tcp_hdr->flags & TCP_HDR_FLAG_SYN) {
        return tcp_handle_receive_syn(pseudo_hdr.src_addr, tcp_hdr, sb, tmp);
    }

    print_dbg(PDBG, STR("Received TCP segment and the checksum is OK!!!\n"));

    return result_ok();
}
