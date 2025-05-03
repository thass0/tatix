#include <tx/arp.h>
#include <tx/ethernet.h>
#include <tx/kvalloc.h>
#include <tx/print.h>

void ethernet_handle_frame(struct netdev *dev, struct byte_view frame)
{
    assert(dev);

    if (frame.len < sizeof(struct ethernet_frame_header)) {
        print_dbg(PDBG, STR("Received frame smaller than ethernet header. Dropping ...\n"));
        return;
    }

    // TODO: Allocation takes time. We should pre-allocate temporary buffers like this. This is also dangerous
    // because the system could be crashed by receiving too many packets and thus running out of memory.
    //
    // NOTE: This arena is large enough to fit a few frames because it will likely be used to store frames but may
    // also be used at times to server other smaller allocations.
    struct byte_array tmp_arn_mem = option_byte_array_checked(kvalloc_alloc(4 * ETHERNET_MAX_FRAME_SIZE, 64));
    struct arena tmp_arn = arena_new(tmp_arn_mem);

    struct ethernet_frame_header *ether_hdr = byte_view_ptr(frame);

    print_dbg(PDBG, STR("Received ethernet frame: dest=%s, src=%s, type=0x%hx\n"),
              mac_addr_format(ether_hdr->dest, &tmp_arn), mac_addr_format(ether_hdr->src, &tmp_arn),
              u16_from_net_u16(ether_hdr->ether_type));

    if (!mac_addr_is_equal(ether_hdr->dest, dev->mac_addr) && !mac_addr_is_equal(ether_hdr->dest, MAC_ADDR_BROADCAST)) {
        print_dbg(PDBG, STR("Received ethernet frame for unknown MAC address %s. Dropping ...\n"),
                  mac_addr_format(ether_hdr->dest, &tmp_arn));
        kvalloc_free(tmp_arn_mem);
        return;
    }

    // TODO: Check if we need to strip anything from the end. We can find this out once we receive frames bigger
    // than 64 bytes (because frames smaller than 64 bytes are padded so we don't know where the data ends).
    struct byte_view payload = byte_view_new(frame.dat + sizeof(struct ethernet_frame_header),
                                             frame.len - sizeof(struct ethernet_frame_header));

    switch (u16_from_net_u16(ether_hdr->ether_type)) {
    case ETHERNET_PTYPE_ARP:
        arp_handle_packet(payload, dev->ip_addr, dev->mac_addr, tmp_arn);
        break;
    default:
        print_dbg(PDBG, STR("Ether type 0x%hx of frame unknown. Dropping ...\n"), ether_hdr->ether_type);
        break;
    }

    kvalloc_free(tmp_arn_mem);
}
