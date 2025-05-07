// Generic network device interface.

#ifndef __TX_NET_NETDEV_H__
#define __TX_NET_NETDEV_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/net/ip_addr.h>
#include <tx/net/mac_addr.h>
#include <tx/net/send_buf.h>
#include <tx/option.h>

// NOTE: The idea behind the `NETDEV_*` constants is that they are independent of any specific protocol. This means
// that numbers used by specific protocols must be converted to the `NETDEV_*` numbers. E.g., the Ethernet type field
// in the Ethernet frame header must be convered to one of the `NETDEV_PROTO_*` constants.
//
// The reason that the `NETDEV_*` constants don't simply count up starting at 0 or at 1 is that debugging is easier
// when working with more recognizable numbers.

typedef u16 netdev_proto_t;
struct_option(netdev_proto_t, netdev_proto_t);

#define NETDEV_PROTO_ARP 0xaa
#define NETDEV_PROTO_IPV4 0x04

typedef u16 netdev_link_type_t;

#define NETDEV_LINK_TYPE_ETHERNET 0xe7

struct netdev;

typedef struct result (*send_frame_func_t)(struct netdev *dev, struct send_buf sb);

struct netdev {
    struct mac_addr mac_addr;
    struct ipv4_addr ip_addr;
    netdev_link_type_t link_type;
    send_frame_func_t send_frame;
    void *private_data;
};

struct input_packet {
    struct mac_addr src; // The MAC address of the sender of this packet.
    struct netdev *netdev; // Interface that this packet was received on.
    netdev_proto_t proto; // Protocol of the data in this packet. See `NETDEV_PROTO_*`.
    struct byte_buf data; // Packet data.
};

// Set a default IP address to use for all new devices.
void netdev_set_default_ip_addr(struct ipv4_addr ip_addr);

// Register a `struct netdev` network device with the `netdev` subsystem. The `mac_addr` and `ip_addr` fields can both
// be used to look up the device. `send_frame` will be called to send a frame using the device. `private_data`
// could, for example, be a driver-specific structure that the network driver needs to function. The `ip_addr`
// field must be 0.0.0.0, indicating that the default IP address can be assigned.
//
// The memory behind the `dev` pointer passed to this function is owned by the driver.
//
// This function should be called by network device drivers to make themselves available for sending frames after they
// have initialized.
struct result netdev_register_device(struct netdev *dev);

// Get a network device by its IP (protocol) address resp. by its MAC address. `netdev_lookup_ip_addr` currently
// crashes when there is more than one match. MAC addresses must be unique anyway.
struct netdev *netdev_lookup_ip_addr(struct ipv4_addr ip_addr);
struct netdev *netdev_lookup_mac_addr(struct mac_addr mac_addr);

// Send a packet of data that uses the protocol `proto`. The destination hardware address is `dest_mac` and the
// interface to send the packet from is `netdev`. The data for the packet should be in `sb`.
struct result netdev_send(struct mac_addr dest_mac, struct netdev *netdev, netdev_proto_t proto, struct send_buf sb);

// Initialize the input queue for received packets. Must be called before calling any of the receive/input-related
// functions.
struct result netdev_init_input_queue(void);

// Receive a packet of data from a device driver. This function can be called inside an interrupt handler. The packet
// will be added to the input queue.
void netdev_intr_receive(struct netdev *netdev, struct byte_view data);

// Try to get the first input packet from the input queue. Call `netdev_release_input` on the packet when you are
// done processing it.
struct input_packet *netdev_get_input(void);

// Remove the given packet from the input queue. This frees up the entry to store a newly received packet in it.
void netdev_release_input(struct input_packet *pkt);

#endif // __TX_NET_NETDEV_H__
