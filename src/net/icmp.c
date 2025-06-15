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

struct icmpv4_echo_message {
    net_u16 ident;
    net_u16 seq;
};

static inline bool icmpv4_checksum_is_ok(struct byte_view message)
{
    // The message contains the checksum so this must be zero (irrespective of endianess).
    return internet_checksum(message).inner == 0;
}

struct result icmpv4_send_echo(struct ipv4_addr dest_addr, u16 ident, u16 seq, struct send_buf sb, struct arena arn)
{
    struct icmpv4_header icmp_hdr;
    icmp_hdr.type = ICMPV4_TYPE_ECHO;
    icmp_hdr.code = 0;
    icmp_hdr.checksum = net_u16_from_u16(0);

    struct icmpv4_echo_message icmp_echo;
    icmp_echo.ident = net_u16_from_u16(ident);
    icmp_echo.seq = net_u16_from_u16(seq);

    sz n_data_bytes = 40;
    struct byte_buf *reply_buf = send_buf_prepend(&sb, sizeof(icmp_hdr) + sizeof(icmp_echo) + n_data_bytes);
    if (!reply_buf)
        return result_error(ENOMEM);

    assert(byte_buf_append(reply_buf, byte_view_new(&icmp_hdr, sizeof(icmp_hdr))) == sizeof(icmp_hdr));
    assert(byte_buf_append(reply_buf, byte_view_new(&icmp_echo, sizeof(icmp_echo))) == sizeof(icmp_echo));
    assert(byte_buf_append_n(reply_buf, n_data_bytes, 0xb0) == n_data_bytes);

    // Patch-in the checksum at the right place.
    net_u16 *dat = byte_buf_ptr(*reply_buf);
    dat[1] = internet_checksum(byte_view_from_buf(*reply_buf));

    print_dbg(PDBG, STR("Sending ICMPv4 echo message to dest_addr=%s ident=0x%hx seq=0x%hx\n"),
              ipv4_addr_format(dest_addr, &arn), ident, seq);

    return ipv4_send_packet(dest_addr, IPV4_PROTOCOL_ICMP, sb, arn);
}

static struct result icmpv4_handle_echo(struct ipv4_addr dest_addr, struct icmpv4_header *hdr, struct byte_view data,
                                        struct send_buf sb, struct arena arn)
{
    if (data.len < sizeof(struct icmpv4_echo_message)) {
        print_dbg(
            PDBG,
            STR("Received ICMPv4 echo message with length too short to fit the identifier and sequence number. Dropping ...\n"));
        return result_ok();
    }

    if (hdr->code != 0) {
        print_dbg(PDBG, STR("Received ICMPv4 echo message with non-zero code. Dropping ...\n"));
        return result_ok();
    }

    struct icmpv4_echo_message *icmp_echo = byte_view_ptr(data);
    print_dbg(PDBG, STR("Received ICMPv4 echo message from %s ident=%hx seq=%hx\n"), ipv4_addr_format(dest_addr, &arn),
              u16_from_net_u16(icmp_echo->ident), u16_from_net_u16(icmp_echo->seq));

    struct icmpv4_header icmp_hdr;
    icmp_hdr.type = ICMPV4_TYPE_ECHO_REPLY;
    icmp_hdr.code = 0;
    icmp_hdr.checksum = net_u16_from_u16(0);

    struct byte_buf *reply_buf = send_buf_prepend(&sb, sizeof(icmp_hdr) + data.len);
    if (!reply_buf)
        return result_error(ENOMEM);

    // We have allocated the reply buffer to be large enough to fit these two appends so they shouldn't fail.
    assert(byte_buf_append(reply_buf, byte_view_new(&icmp_hdr, sizeof(icmp_hdr))) == sizeof(icmp_hdr));
    // An ICMP echo reply message just sends all the data back:
    assert(byte_buf_append(reply_buf, data) == data.len);

    // Patch-in the checksum at the right place.
    net_u16 *dat = byte_buf_ptr(*reply_buf);
    dat[1] = internet_checksum(byte_view_from_buf(*reply_buf));

    print_dbg(PDBG, STR("Sending ICMPv4 echo reply message to dest_addr=%s\n"), ipv4_addr_format(dest_addr, &arn));

    return ipv4_send_packet(dest_addr, IPV4_PROTOCOL_ICMP, sb, arn);
}

static struct result icmpv4_handle_echo_reply(struct ipv4_addr src_addr, struct icmpv4_header *hdr,
                                              struct byte_view data, struct arena arn)
{
    if (data.len < sizeof(struct icmpv4_echo_message)) {
        print_dbg(
            PDBG,
            STR("Received ICMPv4 echo reply message with length too short to fit the identifier and sequence number. Dropping ...\n"));
        return result_ok();
    }

    if (hdr->code != 0) {
        print_dbg(PDBG, STR("Received ICMPv4 echo reply message with non-zero code. Dropping ...\n"));
        return result_ok();
    }

    struct icmpv4_echo_message *icmp_echo = byte_view_ptr(data);
    print_dbg(PDBG, STR("Received ICMPv4 echo reply message from %s ident=0x%hx seq=0x%hx\n"),
              ipv4_addr_format(src_addr, &arn), u16_from_net_u16(icmp_echo->ident), u16_from_net_u16(icmp_echo->seq));

    return result_ok();
}

struct result icmpv4_handle_message(struct ipv4_addr src_addr, struct byte_view message, struct send_buf sb,
                                    struct arena arn)
{
    if (message.len < sizeof(struct icmpv4_header)) {
        print_dbg(PDBG, STR("Received ICMPv4 message smaller than the ICMPv4 header. Dropping ...\n"));
        return result_ok();
    }

    struct icmpv4_header *icmp_hdr = byte_view_ptr(message);

    if (!icmpv4_checksum_is_ok(message)) {
        print_dbg(PDBG, STR("Received ICMPv4 message with invalid checksum. Dropping ...\n"));
        return result_ok();
    }

    struct byte_view data = byte_view_skip(message, sizeof(struct icmpv4_header));

    switch (icmp_hdr->type) {
    case ICMPV4_TYPE_ECHO:
        return icmpv4_handle_echo(src_addr, icmp_hdr, data, sb, arn);
    case ICMPV4_TYPE_ECHO_REPLY:
        return icmpv4_handle_echo_reply(src_addr, icmp_hdr, data, arn);
    default:
        print_dbg(PDBG, STR("Received ICMPv4 message with unknown type 0x%hhx. Dropping ...\n"), icmp_hdr->type);
        return result_ok();
    }
}
