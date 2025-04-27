#include <tx/arp.h>
#include <tx/ethernet.h>
#include <tx/print.h>

void ethernet_handle_frame(struct byte_view frame)
{
    if (frame.len < sizeof(struct ethernet_frame_header)) {
        print_dbg(PDBG, STR("Received frame smaller than ethernet header. Dropping ...\n"));
        return;
    }

    struct ethernet_frame_header *ether_hdr = byte_view_ptr(frame);

    print_dbg(
        PDBG,
        STR("Received ethernet frame: dest=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx, src=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx type=0x%hx\n"),
        ether_hdr->dest.addr[0], ether_hdr->dest.addr[1], ether_hdr->dest.addr[2], ether_hdr->dest.addr[3],
        ether_hdr->dest.addr[4], ether_hdr->dest.addr[5], ether_hdr->src.addr[0], ether_hdr->src.addr[1],
        ether_hdr->src.addr[2], ether_hdr->src.addr[3], ether_hdr->src.addr[4], ether_hdr->src.addr[5],
        u16_from_net_u16(ether_hdr->ether_type));

    // TODO: Check if we need to strip anything from the end. We can find this out once we receive frames bigger
    // than 64 bytes (because frames smaller than 64 bytes are padded so we don't know where the data ends).
    struct byte_view payload = byte_view_new(frame.dat + sizeof(struct ethernet_frame_header),
                                             frame.len - sizeof(struct ethernet_frame_header));

    switch (u16_from_net_u16(ether_hdr->ether_type)) {
    case ETHERNET_PTYPE_ARP:
        arp_handle_packet(payload);
        break;
    default:
        print_dbg(PDBG, STR("Ether type 0x%hx of frame unknown. Dropping ...\n"), ether_hdr->ether_type);
        break;
    }
}
