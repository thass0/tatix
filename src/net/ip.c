#include <tx/byte.h>
#include <tx/net/arp.h>
#include <tx/net/icmp.h>
#include <tx/net/ip.h>
#include <tx/net/netdev.h>

struct ipv4_header {
#if SYSTEM_BYTE_ORDER == NET_BYTE_ORDER
    u8 version : 4;
    u8 ihl : 4; // IHL is length of the IPv4 header in 32-bit words.
#else // SYSTEM_BYTE_ORDER != NET_BYTE_ORDER
    u8 ihl : 4; // IHL is length of the IPv4 header in 32-bit words.
    u8 version : 4;
#endif // SYSTEM_BYTE_ORDER != NET_BYTE_ORDER
    u8 ds_ecn;
    net_u16 total_length; // Length of IPv4 datagram in bytes (including the header).
    net_u16 ident;
    net_u16 fragment_offset;
    u8 ttl;
    u8 protocol;
    net_u16 checksum;
    struct ipv4_addr src_addr;
    struct ipv4_addr dest_addr;
} __packed;

static_assert(sizeof(struct ipv4_header) == 20);

///////////////////////////////////////////////////////////////////////////////
// Internet checksum                                                         //
///////////////////////////////////////////////////////////////////////////////

// NOTE: The internet checksum will always be computed over data that's in network byte order. The properties of the
// internet checksum allow it that despite computing the checksum in host byte order, the result is still in network
// byte order.
net_u16 internet_checksum_iterate(net_u16 checksum, struct byte_view data)
{
    // Refer to RFC 1071 for this algorithm. https://datatracker.ietf.org/doc/html/rfc1071

    u32 sum = checksum.inner;
    u16 *ptr = byte_view_ptr(data);
    sz len = data.len;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len > 0)
        sum += *(u8 *)ptr;

    // Fold the 32-bit sum into the 16-bit range.
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return (net_u16){ sum };
}

net_u16 internet_checksum_finalize(net_u16 sum)
{
    return (net_u16){ ~sum.inner };
}

net_u16 internet_checksum(struct byte_view data)
{
    return internet_checksum_finalize(internet_checksum_iterate(net_u16_from_u16(0), data));
}

static inline bool ipv4_checksum_is_ok(struct ipv4_header *hdr)
{
    return internet_checksum(byte_view_new(hdr, sizeof(struct ipv4_header))).inner == 0;
}

///////////////////////////////////////////////////////////////////////////////
// Handle incoming packets                                                   //
///////////////////////////////////////////////////////////////////////////////

struct result ipv4_handle_packet(struct input_packet *pkt, struct send_buf sb, struct arena arn)
{
    assert(pkt);

    if (pkt->data.len < sizeof(struct ipv4_header)) {
        print_dbg(PDBG, STR("Received IPv4 datagram smaller than the IPv4 header. Dropping ...\n"));
        return result_ok();
    }

    struct ipv4_header *ip_hdr = byte_buf_ptr(pkt->data);

    if (ip_hdr->version != 4) {
        print_dbg(PDBG, STR("Received IPv4 datagram with version %hhu which is different from 4. Dropping ...\n"),
                  ip_hdr->version);
        return result_ok();
    }

    if (!ipv4_checksum_is_ok(ip_hdr)) {
        print_dbg(PDBG, STR("Received IPv4 datagram with invalid checksum. Dropping ...\n"));
        return result_ok();
    }

    // This means we don't accept options:
    if (ip_hdr->ihl * 4 != sizeof(struct ipv4_header)) {
        print_dbg(PDBG, STR("Received IPv4 datagram with IHL %hhu which is different from %lu / 4. Dropping ...\n"),
                  ip_hdr->ihl, sizeof(struct ipv4_header));
        return result_ok();
    }

    if (!ipv4_addr_is_equal(ip_hdr->dest_addr, pkt->netdev->ip_addr)) {
        print_dbg(PWARN, STR("Received IPv4 datagram with destination address %s which is different from %s.\n"),
                  ipv4_addr_format(ip_hdr->dest_addr, &arn), ipv4_addr_format(pkt->netdev->ip_addr, &arn));
    }

    if (u16_from_net_u16(ip_hdr->total_length) > pkt->data.len) {
        print_dbg(
            PDBG,
            STR("Received IPv4 datagram with total length %hu which is larger than the datagram length %ld. Dropping ...\n"),
            u16_from_net_u16(ip_hdr->total_length), pkt->data.len);
        return result_ok();
    }

    struct byte_view payload =
        byte_view_new(pkt->data.dat + sizeof(struct ipv4_header), pkt->data.len - sizeof(struct ipv4_header));

    switch (ip_hdr->protocol) {
    case IPV4_PROTOCOL_ICMP:
        return icmpv4_handle_message(ip_hdr->src_addr, payload, sb, arn);
    default:
        print_dbg(PWARN, STR("Received IPv4 datagram with unknown protocol %hhu. Dropping ...\n"), ip_hdr->protocol);
        return result_ok();
    }
}

///////////////////////////////////////////////////////////////////////////////
// Routing                                                                   //
///////////////////////////////////////////////////////////////////////////////

#define GLOBAL_ROUTE_TABLE_SIZE 32
static struct ipv4_route_entry global_route_table[GLOBAL_ROUTE_TABLE_SIZE];
static bool global_route_table_is_used[GLOBAL_ROUTE_TABLE_SIZE];

struct result ipv4_route_add(struct ipv4_route_entry ent)
{
    for (sz i = 0; i < GLOBAL_ROUTE_TABLE_SIZE; i++) {
        if (!global_route_table_is_used[i]) {
            global_route_table[i] = ent;
            global_route_table_is_used[i] = true;
            return result_ok();
        }
    }

    return result_error(ENOMEM);
}

static i32 count_set_bits(struct ipv4_addr addr)
{
    // This method is known as Kernighan’s Algorithm for counting bits set in intergers.

    // The order of packing this doesn't matter.
    u32 x = (u32)addr.addr[0] | (u32)addr.addr[1] << 8 | (u32)addr.addr[2] << 16 | (u32)addr.addr[3] << 24;
    i32 count = 0;

    while (x) {
        x = x & (x - 1);
        count++;
    }

    return count;
}

static struct ipv4_route_entry *ipv4_route_get_entry(struct ipv4_addr dest_ip)
{
    // This algorithm is based on TCP/IP Illustrated Volume 1, 2nd Edition, Section 5.4.2.

    i32 best_match_n_bits = 0;
    sz best_match_index = 0;
    bool match_found = false;

    for (sz i = 0; i < GLOBAL_ROUTE_TABLE_SIZE; i++) {
        if (global_route_table_is_used[i]) {
            struct ipv4_addr masked = ipv4_addr_mask(dest_ip, global_route_table[i].mask);
            i32 n_bits = count_set_bits(global_route_table[i].mask);
            if (ipv4_addr_is_equal(masked, global_route_table[i].dest) && n_bits >= best_match_n_bits) {
                best_match_index = i;
                best_match_n_bits = n_bits;
                match_found = true;
            }
        }
    }

    if (!match_found)
        return NULL;

    return &global_route_table[best_match_index];
}

///////////////////////////////////////////////////////////////////////////////
// Send packets                                                              //
///////////////////////////////////////////////////////////////////////////////

struct result ipv4_prepend_header(struct ipv4_addr src_ip, struct ipv4_addr dest_ip, u8 proto, struct send_buf *sb)
{
    assert(sizeof(struct ipv4_header) + send_buf_total_length(*sb) <= U16_MAX);

    // NOTE: The current total length of the send buffer is everything that, at the end, will be encapsulated
    // _inside_ the IP header that we construct in this function. Thus, the length of the entire IP packet is
    // the size of the header plus the current total length of the send buffer.

    struct ipv4_header ip_hdr;
    ip_hdr.version = 4;
    ip_hdr.ihl = 5;
    ip_hdr.ds_ecn = 0;
    ip_hdr.total_length = net_u16_from_u16(sizeof(struct ipv4_header) + send_buf_total_length(*sb));
    ip_hdr.ident = net_u16_from_u16(0);
    ip_hdr.fragment_offset = net_u16_from_u16(0);
    ip_hdr.ttl = 64;
    ip_hdr.protocol = proto;
    ip_hdr.checksum = net_u16_from_u16(0);

    ip_hdr.src_addr = src_ip;
    ip_hdr.dest_addr = dest_ip;

    ip_hdr.checksum = internet_checksum(byte_view_new(&ip_hdr, sizeof(ip_hdr)));
    assert(ipv4_checksum_is_ok(&ip_hdr)); // Verify that the checksum is correct.

    struct byte_buf *ip_hdr_buf = send_buf_prepend(sb, sizeof(ip_hdr));
    if (!ip_hdr_buf)
        return result_error(ENOMEM);

    // The buffer `ip_hdr_buf` was allocated to contain `sizeof(ip_hdr)` bytes so this should always succeed.
    assert(byte_buf_append(ip_hdr_buf, byte_view_new(&ip_hdr, sizeof(ip_hdr))) == sizeof(ip_hdr));

    return result_ok();
}

struct result ipv4_send_packet(struct ipv4_addr dest_ip, u8 proto, struct send_buf sb, struct arena arn)
{
    struct ipv4_route_entry *route = ipv4_route_get_entry(dest_ip);
    if (!route)
        return result_error(EHOSTUNREACH);

    struct netdev *netdev = netdev_lookup_ip_addr(route->interface);
    if (!netdev)
        return result_error(ENODEV);

    struct ipv4_addr gateway_ip = route->gateway;
    if (ipv4_addr_is_equal(route->gateway, route->interface)) {
        // When the gateway is this host, we must use direct routing to send the packet directly to the destination.
        gateway_ip = dest_ip;
    }

    struct option_mac_addr gateway_mac_opt = arp_lookup_mac_addr(gateway_ip);
    if (gateway_mac_opt.is_none) {
        print_dbg(PDBG, STR("Missing ARP entry for gateway_ip=%s\n"), ipv4_addr_format(gateway_ip, &arn));
        // Drop all the content that would have been sent inside the IP datagram and tell the caller to try again
        // hoping that next time the ARP entry will be there.
        send_buf_clear(&sb);
        arp_send_request(gateway_ip, netdev, sb, arn);
        return result_error(EAGAIN);
    }

    // Here we need to use the original destination IP address irrespective of what gateway we use (direct or indirect
    // routing).
    struct result res = ipv4_prepend_header(netdev->ip_addr, dest_ip, proto, &sb);
    if (res.is_error)
        return res;

    print_dbg(PDBG, STR("Sending IPv4 packet netdev=%s gateway_ip=%s (%s delivery)\n"),
              mac_addr_format(netdev->mac_addr, &arn), ipv4_addr_format(gateway_ip, &arn),
              ipv4_addr_is_equal(route->gateway, route->interface) ? STR("direct") : STR("indirect"));

    return netdev_send(option_mac_addr_checked(gateway_mac_opt), netdev, NETDEV_PROTO_IPV4, sb);
}
