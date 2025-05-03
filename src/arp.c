// ARP implementation for Ethernet over IPv4.

#include <tx/arp.h>
#include <tx/ethernet.h>
#include <tx/netdev.h>

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

static struct result arp_send_common(u16 opcode, struct ipv4_addr src_ip, struct mac_addr src_mac,
                                     struct ipv4_addr dest_ip, struct mac_addr dest_mac, struct arena arn)
{
    struct byte_buf frame_buf = byte_buf_from_array(byte_array_from_arena(ETHERNET_MAX_FRAME_SIZE, &arn));

    struct ethernet_frame_header ether_hdr;
    ether_hdr.dest = dest_mac;
    ether_hdr.src = src_mac;
    ether_hdr.ether_type = net_u16_from_u16(ETHERNET_PTYPE_ARP);
    byte_buf_append(&frame_buf, byte_view_new(&ether_hdr, sizeof(ether_hdr)));

    struct arp_header arp_hdr;
    arp_hdr.htype = net_u16_from_u16(ARP_HTYPE_ETHERNET);
    arp_hdr.ptype = net_u16_from_u16(ETHERNET_PTYPE_IPV4);
    arp_hdr.hlen = sizeof(struct mac_addr);
    arp_hdr.plen = sizeof(struct ipv4_addr);
    arp_hdr.opcode = net_u16_from_u16(opcode);
    byte_buf_append(&frame_buf, byte_view_new(&arp_hdr, sizeof(arp_hdr)));

    struct ip_ethernet_arp_payload arp_payload;
    arp_payload.src_mac = src_mac;
    arp_payload.src_ip = src_ip;
    arp_payload.dest_mac = dest_mac;
    arp_payload.dest_ip = dest_ip;
    byte_buf_append(&frame_buf, byte_view_new(&arp_payload, sizeof(arp_payload)));

    assert(frame_buf.len == 14 + 8 + 20);

    print_dbg(PINFO, STR("Sending ARP packet (0x%hx). src_ip=%s, src_mac=%s, dest_ip=%s, dest_mac=%s\n"), opcode,
              ipv4_addr_format(src_ip, &arn), mac_addr_format(src_mac, &arn), ipv4_addr_format(dest_ip, &arn),
              mac_addr_format(dest_mac, &arn));

    return netdev_send_frame(src_mac, byte_view_from_buf(frame_buf));
}

struct result arp_send_request(struct ipv4_addr src_ip, struct mac_addr src_mac, struct ipv4_addr dest_ip,
                               struct arena arn)
{
    return arp_send_common(ARP_OPCODE_REQUEST, src_ip, src_mac, dest_ip, MAC_ADDR_BROADCAST, arn);
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

void arp_handle_packet(struct byte_view packet, struct ipv4_addr host_ip, struct mac_addr host_mac, struct arena arn)
{
    if (packet.len < sizeof(struct arp_header) + sizeof(struct ip_ethernet_arp_payload)) {
        print_dbg(PDBG,
                  STR("Received ARP packet smaller than ARP header with IPv4 over Ethernet payload. Dropping ...\n"));
        return;
    }

    struct arp_header *arp_hdr = byte_view_ptr(packet);

    if (u16_from_net_u16(arp_hdr->htype) != ARP_HTYPE_ETHERNET ||
        u16_from_net_u16(arp_hdr->ptype) != ETHERNET_PTYPE_IPV4) {
        print_dbg(PDBG, STR("Received ARP packet with unknown htype=0x%hx or ptype=0x%hx. Dropping ...\n"),
                  u16_from_net_u16(arp_hdr->htype), u16_from_net_u16(arp_hdr->ptype));
        return;
    }

    if (arp_hdr->hlen != sizeof(struct mac_addr) || arp_hdr->plen != sizeof(struct ipv4_addr)) {
        print_dbg(
            PWARN,
            STR("Received ARP packet with hlen=%hhu and plen=%hhu. These are wrong for IPv4 over Ethernet. Continuing assuming hlen=6 and plen=4\n"),
            arp_hdr->hlen, arp_hdr->plen);
    }

    struct ip_ethernet_arp_payload *payload =
        (struct ip_ethernet_arp_payload *)(packet.dat + sizeof(struct arp_header));

    struct option_mac_addr old_mac_opt = arp_lookup_mac_addr(payload->src_ip);

    struct result_bool insert_res = arp_table_update_or_insert(payload->src_ip, payload->src_mac);
    if (insert_res.is_error) {
        print_dbg(PWARN, STR("Failed to update ARP table: 0x%hx\n"), insert_res.code);
        return;
    }

    print_dbg(PINFO, STR("Updated ARP table with ip_addr=%s, mac_addr=%s (old mac_addr=%s)\n"),
              ipv4_addr_format(payload->src_ip, &arn), mac_addr_format(payload->src_mac, &arn),
              result_bool_checked(insert_res) ? mac_addr_format(option_mac_addr_checked(old_mac_opt), &arn) :
                                                STR("none"));

    // The reply has the `src_*` and `dest_*` fields swapped from the incoming packet and uses the source fields
    // of the host.
    if (u16_from_net_u16(arp_hdr->opcode) == ARP_OPCODE_REQUEST)
        arp_send_common(ARP_OPCODE_REPLY, host_ip, host_mac, payload->src_ip, payload->src_mac, arn);
}
