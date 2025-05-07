#include <tx/byte.h>
#include <tx/net/arp.h>
#include <tx/net/icmp.h>
#include <tx/net/ip.h>
#include <tx/net/netdev.h>

struct ipv4_header {
#if SYSTEM_BYTE_ORDER == ORDER_LITTLE_ENDIAN
    u8 ihl : 4; // IHL is length of the IPv4 header in 32-bit words.
    u8 version : 4;
#else // SYSTEM_BYTE_ORDER != ORDER_LITTLE_ENDIAN
    u8 version : 4;
    u8 ihl : 4; // IHL is length of the IPv4 header in 32-bit words.
#endif // SYSTEM_BYTE_ORDER != ORDER_LITTLE_ENDIAN
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

// NOTE: The internet checksum will always be computed over data that's in network byte order. The properties of the
// internet checksum allow it that despite computing the checksum in host byte order, the result is still in network
// byte order.
net_u16 internet_checksum(struct byte_view data)
{
    // Refer to RFC 1071 for this algorithm. https://datatracker.ietf.org/doc/html/rfc1071

    u32 sum = 0;
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

    return (net_u16){ ~sum };
}

static inline bool ipv4_checksum_is_ok(struct ipv4_header *hdr)
{
    return internet_checksum(byte_view_new(hdr, sizeof(struct ipv4_header))).inner == 0;
}

void ipv4_handle_packet(struct input_packet *pkt, struct send_buf sb, struct arena arn)
{
    assert(pkt);

    if (pkt->data.len < sizeof(struct ipv4_header)) {
        print_dbg(PDBG, STR("Received IPv4 datagram smaller than the IPv4 header. Dropping ...\n"));
        return;
    }

    struct ipv4_header *ip_hdr = byte_buf_ptr(pkt->data);

    if (ip_hdr->version != 4) {
        print_dbg(PDBG, STR("Received IPv4 datagram with version %hhu which is different from 4. Dropping ...\n"),
                  ip_hdr->version);
        return;
    }

    if (!ipv4_checksum_is_ok(ip_hdr)) {
        print_dbg(PDBG, STR("Received IPv4 datagram with invalid checksum. Dropping ...\n"));
        return;
    }

    // This means we don't accept options:
    if (ip_hdr->ihl * 4 != sizeof(struct ipv4_header)) {
        print_dbg(PDBG, STR("Received IPv4 datagram with IHL %hhu which is different from %lu / 4. Dropping ...\n"),
                  ip_hdr->ihl, sizeof(struct ipv4_header));
        return;
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
        return;
    }

    struct byte_view payload =
        byte_view_new(pkt->data.dat + sizeof(struct ipv4_header), pkt->data.len - sizeof(struct ipv4_header));

    switch (ip_hdr->protocol) {
    case IPV4_PROTOCOL_ICMP:
        icmpv4_handle_message(ip_hdr->src_addr, payload, sb, arn);
        break;
    default:
        print_dbg(PWARN, STR("Received IPv4 datagram with unknown protocol %hhu. Dropping ...\n"), ip_hdr->protocol);
        break;
    }
}

struct result ipv4_send_packet(struct ipv4_addr dest_ip, u8 proto, struct send_buf sb, struct arena arn __unused)
{
    // TODO: Routing.
    struct netdev *netdev = netdev_lookup_ip_addr(ipv4_addr_new(192, 168, 100, 2));
    if (!netdev)
        return result_error(ENODEV);

    struct option_mac_addr dest_mac_opt = arp_lookup_mac_addr(dest_ip);
    if (dest_mac_opt.is_none)
        return result_error(EHOSTUNREACH);

    struct ipv4_header ip_hdr;
    ip_hdr.version = 4;
    ip_hdr.ihl = 5;
    ip_hdr.ds_ecn = 0;
    assert(sizeof(struct ipv4_header) + send_buf_total_length(sb) <= U16_MAX);
    ip_hdr.total_length = net_u16_from_u16(sizeof(struct ipv4_header) + send_buf_total_length(sb));
    ip_hdr.ident = net_u16_from_u16(0);
    ip_hdr.fragment_offset = net_u16_from_u16(0);
    ip_hdr.ttl = 64;
    ip_hdr.protocol = proto;
    ip_hdr.checksum = net_u16_from_u16(0);
    ip_hdr.src_addr = netdev->ip_addr;
    ip_hdr.dest_addr = dest_ip;

    ip_hdr.checksum = internet_checksum(byte_view_new(&ip_hdr, sizeof(ip_hdr)));
    assert(ipv4_checksum_is_ok(&ip_hdr));

    struct byte_buf *ip_hdr_buf = send_buf_prepend(&sb, sizeof(ip_hdr));
    if (!ip_hdr_buf)
        return result_error(ENOMEM);

    // The buffer `ip_hdr_buf` was allocated to contain `sizeof(ip_hdr)` bytes so this should always succeed.
    assert(byte_buf_append(ip_hdr_buf, byte_view_new(&ip_hdr, sizeof(ip_hdr))) == sizeof(ip_hdr));

    return netdev_send(option_mac_addr_checked(dest_mac_opt), netdev, NETDEV_PROTO_IPV4, sb);
}
