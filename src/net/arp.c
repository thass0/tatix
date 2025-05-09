// ARP implementation for Ethernet over IPv4.

#include <tx/net/arp.h>
#include <tx/net/ethernet.h>
#include <tx/net/netdev.h>

struct arp_table_ent {
    bool is_used;
    struct ipv4_addr ip_addr;
    struct mac_addr mac_addr;
};

#define GLOBAL_ARP_TABLE_SIZE 32
static struct arp_table_ent global_arp_table[GLOBAL_ARP_TABLE_SIZE];

struct ip_ethernet_arp_payload {
    struct mac_addr src_mac;
    struct ipv4_addr src_ip;
    struct mac_addr dest_mac;
    struct ipv4_addr dest_ip;
} __packed;

static_assert(sizeof(struct ip_ethernet_arp_payload) == 20);

static struct result arp_send_common(u16 opcode, struct ipv4_addr dest_ip, struct mac_addr dest_mac,
                                     struct netdev *netdev, struct send_buf sb, struct arena tmp)
{
    assert(netdev);

    struct arp_header arp_hdr;
    arp_hdr.htype = net_u16_from_u16(ARP_HTYPE_ETHERNET);
    arp_hdr.ptype = net_u16_from_u16(ETHERNET_PTYPE_IPV4);
    arp_hdr.hlen = sizeof(struct mac_addr);
    arp_hdr.plen = sizeof(struct ipv4_addr);
    arp_hdr.opcode = net_u16_from_u16(opcode);

    struct ip_ethernet_arp_payload arp_payload;
    arp_payload.src_mac = netdev->mac_addr;
    arp_payload.src_ip = netdev->ip_addr;
    arp_payload.dest_mac = dest_mac;
    arp_payload.dest_ip = dest_ip;

    struct byte_buf *buf = send_buf_prepend(&sb, sizeof(arp_hdr) + sizeof(arp_payload));

    byte_buf_append(buf, byte_view_new(&arp_hdr, sizeof(arp_hdr)));
    byte_buf_append(buf, byte_view_new(&arp_payload, sizeof(arp_payload)));

    assert(buf->len == 8 + 20);

    print_dbg(PDBG, STR("Sending ARP packet (0x%hx). src_ip=%s, src_mac=%s, dest_ip=%s, dest_mac=%s\n"), opcode,
              ipv4_addr_format(netdev->ip_addr, &tmp), mac_addr_format(netdev->mac_addr, &tmp),
              ipv4_addr_format(dest_ip, &tmp), mac_addr_format(dest_mac, &tmp));

    return netdev_send(dest_mac, netdev, NETDEV_PROTO_ARP, sb);
}

struct result arp_send_request(struct ipv4_addr dest_ip, struct netdev *netdev, struct send_buf sb, struct arena tmp)
{
    assert(netdev);
    return arp_send_common(ARP_OPCODE_REQUEST, dest_ip, MAC_ADDR_BROADCAST, netdev, sb, tmp);
}

struct option_mac_addr arp_lookup_mac_addr(struct ipv4_addr ip_addr)
{
    sz n_matches = 0;
    struct arp_table_ent *last_match = NULL;

    for (sz i = 0; i < GLOBAL_ARP_TABLE_SIZE; i++) {
        if (global_arp_table[i].is_used && ipv4_addr_is_equal(global_arp_table[i].ip_addr, ip_addr)) {
            n_matches++;
            last_match = &global_arp_table[i];
        }
    }

    // This is guaranteed by `arp_table_update_or_insert`.
    assert(n_matches <= 1);

    if (n_matches == 0)
        return option_mac_addr_none();

    return option_mac_addr_ok(last_match->mac_addr);
}

static struct result_bool arp_table_update_or_insert(struct ipv4_addr ip_addr, struct mac_addr mac_addr)
{
    // If the given IPv4 address already has an entry in the table, we just need to update its associated MAC address.
    for (sz i = 0; i < GLOBAL_ARP_TABLE_SIZE; i++) {
        if (global_arp_table[i].is_used && ipv4_addr_is_equal(ip_addr, global_arp_table[i].ip_addr)) {
            global_arp_table[i].mac_addr = mac_addr;
            return result_bool_ok(true);
        }
    }

    // There is no entry for the given IPv4 address in the table so we can create a new one.
    for (sz i = 0; i < GLOBAL_ARP_TABLE_SIZE; i++) {
        if (!global_arp_table[i].is_used) {
            global_arp_table[i].is_used = true;
            global_arp_table[i].ip_addr = ip_addr;
            global_arp_table[i].mac_addr = mac_addr;
            return result_bool_ok(false);
        }
    }

    return result_bool_error(ENOMEM);
}

struct result arp_handle_packet(struct input_packet *pkt, struct send_buf sb, struct arena tmp)
{
    if (pkt->data.len < sizeof(struct arp_header) + sizeof(struct ip_ethernet_arp_payload)) {
        print_dbg(PDBG,
                  STR("Received ARP packet smaller than ARP header with IPv4 over Ethernet payload. Dropping ...\n"));
        return result_ok();
    }

    struct arp_header *arp_hdr = byte_buf_ptr(pkt->data);

    if (u16_from_net_u16(arp_hdr->htype) != ARP_HTYPE_ETHERNET ||
        u16_from_net_u16(arp_hdr->ptype) != ETHERNET_PTYPE_IPV4) {
        print_dbg(PDBG, STR("Received ARP packet with unknown htype=0x%hx or ptype=0x%hx. Dropping ...\n"),
                  u16_from_net_u16(arp_hdr->htype), u16_from_net_u16(arp_hdr->ptype));
        return result_ok();
    }

    if (arp_hdr->hlen != sizeof(struct mac_addr) || arp_hdr->plen != sizeof(struct ipv4_addr)) {
        print_dbg(
            PWARN,
            STR("Received ARP packet with hlen=%hhu and plen=%hhu. These are wrong for IPv4 over Ethernet. Continuing assuming hlen=6 and plen=4\n"),
            arp_hdr->hlen, arp_hdr->plen);
    }

    struct ip_ethernet_arp_payload *payload =
        (struct ip_ethernet_arp_payload *)(pkt->data.dat + sizeof(struct arp_header));

    struct option_mac_addr old_mac_opt = arp_lookup_mac_addr(payload->src_ip);

    struct result_bool insert_res = arp_table_update_or_insert(payload->src_ip, payload->src_mac);
    if (insert_res.is_error) {
        print_dbg(PWARN, STR("Failed to update ARP table: 0x%hx\n"), insert_res.code);
        return result_error(insert_res.code);
    }

    print_dbg(PDBG, STR("Received ARP packet and updated ARP table with ip_addr=%s, mac_addr=%s (old mac_addr=%s)\n"),
              ipv4_addr_format(payload->src_ip, &tmp), mac_addr_format(payload->src_mac, &tmp),
              result_bool_checked(insert_res) ? mac_addr_format(option_mac_addr_checked(old_mac_opt), &tmp) :
                                                STR("none"));

    // The reply contains the `src_*` fields of the incoming packet as the destination.
    if (u16_from_net_u16(arp_hdr->opcode) == ARP_OPCODE_REQUEST)
        return arp_send_common(ARP_OPCODE_REPLY, payload->src_ip, payload->src_mac, pkt->netdev, sb, tmp);
    return result_ok();
}
