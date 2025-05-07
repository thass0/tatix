#include <tx/byte.h>
#include <tx/net/icmp.h>
#include <tx/net/ip.h>
#include <tx/print.h>

#define ICMPV4_TYPE_ECHO_REPLY 0
#define ICMPV4_TYPE_ECHO 8

struct icmpv4_header {
    u8 type;
    u8 code;
    net_u16 checksum;
};

static inline bool icmpv4_checksum_is_ok(struct byte_view message)
{
    // The message contains the checksum so this must be zero (irrespective of endianess).
    return internet_checksum(message).inner == 0;
}

static void icmpv4_handle_echo_request(struct ipv4_addr reply_to_addr, struct icmpv4_header *hdr, struct byte_view data,
                                       struct send_buf sb, struct arena arn)
{
    if (data.len < 4) {
        print_dbg(
            PDBG,
            STR("Received ICMPv4 echo request with length too short to fit the identifier and sequence number. Dropping ...\n"));
        return;
    }

    if (hdr->code != 0) {
        print_dbg(PDBG, STR("Received ICMPv4 echo request with non-zero code. Dropping ...\n"));
        return;
    }

    struct byte_buf *reply_buf = send_buf_prepend(&sb, sizeof(struct icmpv4_header) + data.len);
    if (!reply_buf)
        return;

    struct icmpv4_header icmp_hdr;
    icmp_hdr.type = ICMPV4_TYPE_ECHO_REPLY;
    icmp_hdr.code = 0;
    icmp_hdr.checksum = net_u16_from_u16(0);

    // We have allocated the reply buffer to be large enough to fit these two appends so they shouldn't fail.
    assert(byte_buf_append(reply_buf, byte_view_new(&icmp_hdr, sizeof(icmp_hdr))) == sizeof(icmp_hdr));
    // An ICMP echo reply just sends all the data back:
    assert(byte_buf_append(reply_buf, data) == data.len);

    // Patch-in the checksum at the right place.
    net_u16 *dat = byte_buf_ptr(*reply_buf);
    dat[1] = internet_checksum(byte_view_from_buf(*reply_buf));

    ipv4_send_packet(reply_to_addr, IPV4_PROTOCOL_ICMP, sb, arn);
}

void icmpv4_handle_message(struct ipv4_addr src_addr, struct byte_view message, struct send_buf sb, struct arena arn)
{
    if (message.len < sizeof(struct icmpv4_header)) {
        print_dbg(PDBG, STR("Received ICMPv4 message smaller than the ICMPv4 header. Dropping ...\n"));
        return;
    }

    struct icmpv4_header *icmp_hdr = byte_view_ptr(message);

    if (!icmpv4_checksum_is_ok(message)) {
        print_dbg(PDBG, STR("Received ICMPv4 message with invalid checksum. Dropping ...\n"));
        return;
    }

    struct byte_view data =
        byte_view_new(message.dat + sizeof(struct icmpv4_header), message.len - sizeof(struct icmpv4_header));

    switch (icmp_hdr->type) {
    case ICMPV4_TYPE_ECHO:
        icmpv4_handle_echo_request(src_addr, icmp_hdr, data, sb, arn);
        break;
    default:
        print_dbg(PDBG, STR("Received ICMPv4 message with unknown type 0x%hhx. Dropping ...\n"), icmp_hdr->type);
        break;
    }
}
