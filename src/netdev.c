#include <tx/arp.h>
#include <tx/asm.h>
#include <tx/ethernet.h>
#include <tx/kvalloc.h>
#include <tx/netdev.h>
#include <tx/print.h>

///////////////////////////////////////////////////////////////////////////////
// Device registration and lookup                                            //
///////////////////////////////////////////////////////////////////////////////

#define NETDEV_TABLE_SIZE 16
static bool global_netdev_table_used[NETDEV_TABLE_SIZE];
static struct netdev *global_netdev_table[NETDEV_TABLE_SIZE];

static struct ipv4_addr global_netdev_default_ip_addr;

void netdev_set_default_ip_addr(struct ipv4_addr ip_addr)
{
    global_netdev_default_ip_addr = ip_addr;
}

struct result netdev_register_device(struct netdev *dev)
{
    assert(dev);

    struct ipv4_addr zero = ipv4_addr_new(0, 0, 0, 0);

    if (ipv4_addr_is_equal(global_netdev_default_ip_addr, zero))
        return result_error(EINVAL);

    // The device isn't allowed to set a custom IP address:
    if (!ipv4_addr_is_equal(dev->ip_addr, zero))
        return result_error(EINVAL);

    dev->ip_addr = global_netdev_default_ip_addr;

    byte fmt_buf[2 * MAC_ADDR_FMT_BUF_SIZE + IP_ADDR_FMT_BUF_SIZE];
    struct arena fmt_arn = arena_new(byte_array_new(fmt_buf, countof(fmt_buf)));

    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i] && mac_addr_is_equal(dev->mac_addr, global_netdev_table[i]->mac_addr)) {
            print_dbg(PDBG, STR("Device with MAC address %s already exists\n"),
                      mac_addr_format(dev->mac_addr, &fmt_arn));
            return result_error(EEXIST);
        }
    }

    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (!global_netdev_table_used[i]) {
            global_netdev_table_used[i] = true;
            global_netdev_table[i] = dev;
            print_dbg(PINFO, STR("Registered device with MAC address %s and IP address %s\n"),
                      mac_addr_format(dev->mac_addr, &fmt_arn), ipv4_addr_format(dev->ip_addr, &fmt_arn));
            return result_ok();
        }
    }

    return result_error(ENOMEM);
}

struct netdev *netdev_lookup_ip_addr(struct ipv4_addr addr)
{
    // NOTE: IP addresses aren't necessarily unique. We will handle the case of multiple IP addresses when it properly
    // arises for the first time (hence the warning).
    sz n_matches = 0;
    struct netdev *last_match = NULL;

    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i] && ipv4_addr_is_equal(addr, global_netdev_table[i]->ip_addr)) {
            n_matches++;
            last_match = global_netdev_table[i];
        }
    }

    if (n_matches > 1) {
        byte backing[32];
        struct arena tmp = arena_new(byte_array_new(backing, sizeof(backing)));
        print_dbg(PWARN, STR("Found more than one device for IP address %s. Returning the last one\n"),
                  ipv4_addr_format(addr, &tmp));
    }

    return last_match;
}

struct netdev *netdev_lookup_mac_addr(struct mac_addr addr)
{
    // NOTE: MAC addresses are unique in the netdev table (see `netdev_register_device` function).
    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i] && mac_addr_is_equal(addr, global_netdev_table[i]->mac_addr)) {
            return global_netdev_table[i];
        }
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Send data                                                                 //
///////////////////////////////////////////////////////////////////////////////

static struct result netdev_append_link_header(struct byte_buf *buf, struct netdev *netdev, struct mac_addr dest_mac,
                                               netdev_proto_t proto)
{
    assert(netdev->link_type == NETDEV_LINK_TYPE_ETHERNET); // We don't support anything else at this point.

    struct option_u16 ether_type_opt = ethernet_type_from_netdev_proto(proto);
    if (ether_type_opt.is_none)
        return result_error(EINVAL);

    struct ethernet_frame_header ether_hdr;
    ether_hdr.dest = dest_mac;
    ether_hdr.src = netdev->mac_addr;
    ether_hdr.ether_type = net_u16_from_u16(option_u16_checked(ether_type_opt));

    sz n_appended = byte_buf_append(buf, byte_view_new(&ether_hdr, sizeof(ether_hdr)));
    if (n_appended != sizeof(ether_hdr))
        return result_error(ENOMEM);

    return result_ok();
}

struct result netdev_send(struct mac_addr dest_mac, struct netdev *netdev, netdev_proto_t proto, struct send_buf sb)
{
    assert(netdev->link_type == NETDEV_LINK_TYPE_ETHERNET); // We don't support anything else at this point.
    struct byte_buf *buf = send_buf_prepend(&sb, sizeof(struct ethernet_frame_header));
    if (!buf)
        return result_error(ENOMEM);

    struct result res = netdev_append_link_header(buf, netdev, dest_mac, proto);
    if (res.is_error)
        return res;

    res = netdev->send_frame(netdev, sb);
    if (res.is_error)
        return res;

    return result_ok();
}

///////////////////////////////////////////////////////////////////////////////
// Input (receive) queue                                                     //
///////////////////////////////////////////////////////////////////////////////

#define NETDEV_INPUT_PACKET_SIZE 2048
#define NETDEV_INPUT_QUEUE_SIZE 64
static struct input_packet global_input_queue[NETDEV_INPUT_QUEUE_SIZE];
static sz global_input_queue_tail;
static sz global_input_queue_head;
static bool global_input_queue_is_initialized;

// NOTE: On the head and tail semantics of the queue. The head points to the next position where a new packet
// will be stored. The tail points to the first stored packet that hasn't been processed yet. The queue is
// considered empty when `head == tail`. This means that:
//
//   (1) To add a new incoming packet, first ensure that `(head + 1) % size != tail`, meaning there is space
//       for at least one more packet. If there's space, store the new packet in `queue[head]`, then update
//       `head = (head + 1) % size`.
//   (2) To remove a packet that has been processed, first ensure that `head != tail`, meaning the queue is not
//       empty. If not empty, process the packet at `queue[tail]`, then update `tail = (tail + 1) % size`.
//       When `head == tail` after removing a packet, the queue becomes empty.

struct result netdev_init_input_queue(void)
{
    struct option_byte_array alloc = option_byte_array_none();

    // We allocate a data buffer for every entry in the queue. This buffer never changes, it's content is just
    // updated every time a new packet is stored in it. The other fields remain zero-initialized.

    for (sz i = 0; i < NETDEV_INPUT_QUEUE_SIZE; i++) {
        alloc = kvalloc_alloc(NETDEV_INPUT_PACKET_SIZE, 64);
        if (alloc.is_none) {
            for (sz j = 0; j < i; j++)
                kvalloc_free(byte_array_new(global_input_queue[j].data.dat, global_input_queue[j].data.cap));
            return result_error(ENOMEM);
        }
        global_input_queue[i].data = byte_buf_from_array(option_byte_array_checked(alloc));
    }

    global_input_queue_tail = 0;
    global_input_queue_head = 0;

    global_input_queue_is_initialized = true;

    return result_ok();
}

static struct result netdev_intr_input_queue_add(struct mac_addr src, struct netdev *netdev, netdev_proto_t proto,
                                                 struct byte_view data)
{
    if ((global_input_queue_head + 1) % NETDEV_INPUT_QUEUE_SIZE == global_input_queue_tail)
        return result_error(EAGAIN);

    struct input_packet *pkt = &global_input_queue[global_input_queue_head];
    pkt->src = src;
    pkt->netdev = netdev;
    pkt->proto = proto;
    pkt->data.len = 0;
    byte_buf_append(&pkt->data, data);

    global_input_queue_head = (global_input_queue_head + 1) % NETDEV_INPUT_QUEUE_SIZE;

    return result_ok();
}

static void netdev_intr_receive_ethernet(struct netdev *netdev, struct byte_view frame)
{
    assert(netdev);

    // Frame must be large enough to fit the ethernet header.
    if (frame.len < sizeof(struct ethernet_frame_header))
        return;

    struct ethernet_frame_header *ether_hdr = byte_view_ptr(frame);

    // Drop packets with a different destination address than the MAC address of the netdev.
    if (!mac_addr_is_equal(ether_hdr->dest, netdev->mac_addr) &&
        !mac_addr_is_equal(ether_hdr->dest, MAC_ADDR_BROADCAST))
        return;

    // TODO: Check if we need to strip anything from the end. We can find this out once we receive frames bigger
    // than 64 bytes (because frames smaller than 64 bytes are padded so we don't know where the data ends).
    struct byte_view payload = byte_view_new(frame.dat + sizeof(struct ethernet_frame_header),
                                             frame.len - sizeof(struct ethernet_frame_header));

    struct option_netdev_proto_t proto_opt = netdev_proto_from_ethernet_type(u16_from_net_u16(ether_hdr->ether_type));
    if (proto_opt.is_none)
        return;

    netdev_intr_input_queue_add(ether_hdr->src, netdev, option_netdev_proto_t_checked(proto_opt), payload);
}

void netdev_intr_receive(struct netdev *netdev, struct byte_view frame)
{
    assert(global_input_queue_is_initialized);
    assert(netdev->link_type == NETDEV_LINK_TYPE_ETHERNET); // TODO: Support other link types.

    netdev_intr_receive_ethernet(netdev, frame);
}

struct input_packet *netdev_get_input(void)
{
    assert(global_input_queue_is_initialized);

    // We can't allow interrupts to fire while looking at the head index because the head index is incremented in
    // the receive interrupt handler.
    disable_interrupts();

    if (global_input_queue_tail == global_input_queue_head) {
        enable_interrupts();
        return NULL; // The queue is empty.
    }

    // Interrupts can be enabled while processing the packet because the receive interrupt handler won't modify
    // the tail index.
    enable_interrupts();

    return &global_input_queue[global_input_queue_tail];
}

void netdev_release_input(struct input_packet *pkt)
{
    assert(global_input_queue_is_initialized);

    assert(pkt == &global_input_queue[global_input_queue_tail]);

    // We can't allow interrupts to fire while updating the tail index because the update depends on the state
    // of the head index, but the head index is incremented in the receive interrupt handler.
    disable_interrupts();

    if (global_input_queue_tail == global_input_queue_head) {
        enable_interrupts();
        return;
    }

    global_input_queue_tail = (global_input_queue_tail + 1) % NETDEV_INPUT_QUEUE_SIZE;

    enable_interrupts();
}
