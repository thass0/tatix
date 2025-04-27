#include <tx/arp.h>
#include <tx/netdev.h>

struct arp_table_ent {
    bool is_used;
    struct ip_addr ip_addr;
    struct mac_addr mac_addr;
};

#define GLOBAL_ARP_TABLE_SIZE 32
static struct arp_table_ent global_arp_table[GLOBAL_ARP_TABLE_SIZE];

struct result arp_send_request(struct ip_addr src_ip, struct mac_addr src_mac, struct ip_addr dest_ip, struct arena arn)
{
    struct byte_buf frame_buf = byte_buf_from_array(byte_array_from_arena(ETHERNET_MAX_FRAME_SIZE, &arn));

    struct ethernet_frame_header ether_hdr;
    ether_hdr.dest = MAC_ADDR_BROADCAST;
    ether_hdr.src = src_mac;
    ether_hdr.ether_type = net_u16_from_u16(ETHERNET_PTYPE_ARP);
    byte_buf_append(&frame_buf, byte_view_new(&ether_hdr, sizeof(ether_hdr)));

    struct arp_header arp_hdr;
    arp_hdr.htype = net_u16_from_u16(ARP_HTYPE_ETHERNET);
    arp_hdr.ptype = net_u16_from_u16(ETHERNET_PTYPE_IPV4);
    arp_hdr.hlen = sizeof(struct mac_addr);
    arp_hdr.plen = sizeof(struct ip_addr);
    arp_hdr.opcode = net_u16_from_u16(ARP_OPCODE_REQUEST);
    byte_buf_append(&frame_buf, byte_view_new(&arp_hdr, sizeof(arp_hdr)));

    byte_buf_append(&frame_buf, byte_view_new(&src_mac, sizeof(src_mac)));
    byte_buf_append(&frame_buf, byte_view_new(&src_ip, sizeof(src_ip)));
    byte_buf_append_n(&frame_buf, sizeof(struct mac_addr), 0);
    byte_buf_append(&frame_buf, byte_view_new(&dest_ip, sizeof(dest_ip)));

    print_dbg(PINFO, STR("Broadcasting ARP request for IP address %hhd.%hhd.%hhd.%hhd\n"), dest_ip.addr[0],
              dest_ip.addr[1], dest_ip.addr[2], dest_ip.addr[3]);

    return netdev_send_frame(src_mac, byte_view_from_buf(frame_buf));
}

struct result_mac_addr arp_lookup_mac_addr(struct ip_addr ip_addr)
{
    sz n_matches = 0;
    struct arp_table_ent *last_match = NULL;

    for (sz i = 0; i < GLOBAL_ARP_TABLE_SIZE; i++) {
        if (global_arp_table[i].is_used && ip_addr_is_equal(global_arp_table[i].ip_addr, ip_addr)) {
            n_matches++;
            last_match = &global_arp_table[i];
        }
    }

    assert(n_matches <= 1);

    if (n_matches == 0)
        return result_mac_addr_error(EHOSTUNREACH);

    return result_mac_addr_ok(last_match->mac_addr);
}

static void arp_table_insert(struct mac_addr mac, struct ip_addr ip)
{
    for (sz i = 0; i < GLOBAL_ARP_TABLE_SIZE; i++) {
        if (global_arp_table[i].is_used && mac_addr_is_equal(mac, global_arp_table[i].mac_addr) &&
            ip_addr_is_equal(ip, global_arp_table[i].ip_addr)) {
            print_dbg(PDBG, STR("ARP table already contains this entry. Dropping ...\n"));
            return;
        }
    }

    for (sz i = 0; i < GLOBAL_ARP_TABLE_SIZE; i++) {
        if (!global_arp_table[i].is_used) {
            global_arp_table[i].is_used = true;
            global_arp_table[i].mac_addr = mac;
            global_arp_table[i].ip_addr = ip;
            print_dbg(PINFO, STR(" ... Reply inserted into ARP table\n"));
            return;
        }
    }

    print_dbg(PDBG, STR("ARP table full. Dropping ...\n"));
}

static void arp_handle_reply(struct byte_view payload)
{
    if (payload.len < 2 * sizeof(struct mac_addr) + 2 * sizeof(struct ip_addr)) {
        print_dbg(PDBG, STR("Received ARP reply too small to hold the addresses. Dropping ...\n"));
        return;
    }

    struct mac_addr src_mac = *(struct mac_addr *)payload.dat;
    struct ip_addr src_ip = *(struct ip_addr *)(payload.dat + sizeof(struct mac_addr));

    print_dbg(PINFO, STR("Received ARP reply from %hhd.%hhd.%hhd.%hhd (%hhx:%hhx:%hhx:%hhx:%hhx:%hhx)\n"),
              src_ip.addr[0], src_ip.addr[1], src_ip.addr[2], src_ip.addr[3], src_mac.addr[0], src_mac.addr[1],
              src_mac.addr[2], src_mac.addr[3], src_mac.addr[4], src_mac.addr[5]);

    arp_table_insert(src_mac, src_ip);
}

static void arp_handle_request(struct byte_view payload __unused)
{
    crash("TODO");
}

void arp_handle_packet(struct byte_view packet)
{
    if (packet.len < sizeof(struct arp_header)) {
        print_dbg(PDBG, STR("Received ARP packet smaller than ARP header. Dropping ...\n"));
        return;
    }

    struct arp_header *arp_hdr = byte_view_ptr(packet);

    if (u16_from_net_u16(arp_hdr->htype) != ARP_HTYPE_ETHERNET ||
        u16_from_net_u16(arp_hdr->ptype) != ETHERNET_PTYPE_IPV4) {
        print_dbg(PDBG, STR("Received ARP packet with unknown htype=0x%hx or ptype=0x%hx. Dropping ...\n"),
                  u16_from_net_u16(arp_hdr->htype), u16_from_net_u16(arp_hdr->ptype));
    }

    if (arp_hdr->hlen != sizeof(struct mac_addr) || arp_hdr->plen != sizeof(struct ip_addr)) {
        print_dbg(
            PWARN,
            STR("Received ARP packet with hlen=%hhu and plen=%hhu. These are wrong for Ethernet and IPv4. Continuing assuming hlen=6 and plen=4\n"),
            arp_hdr->hlen, arp_hdr->plen);
    }

    struct byte_view payload =
        byte_view_new(packet.dat + sizeof(struct arp_header), packet.len - sizeof(struct arp_header));

    switch (u16_from_net_u16(arp_hdr->opcode)) {
    case ARP_OPCODE_REPLY:
        arp_handle_reply(payload);
        break;
    case ARP_OPCODE_REQUEST:
        arp_handle_request(payload);
        break;
    default:
        print_dbg(PDBG, STR("ARP packet has unknown opcode=0x%hx. Dropping ...\n"), u16_from_net_u16(arp_hdr->opcode));
        break;
    }
}
