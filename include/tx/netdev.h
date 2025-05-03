// Generic network device interface.

#ifndef __TX_NETDEV_H__
#define __TX_NETDEV_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/mac.h>
#include <tx/ip.h>

struct netdev;

typedef struct result (*send_frame_func_t)(struct netdev *dev, struct byte_view frame);

struct netdev {
    struct mac_addr mac_addr;
    send_frame_func_t send_frame;
    void *private_data;
};

// Send a frame (only Ethernet right now) with data `frame` using the device with MAC address `addr`.
struct result netdev_send_frame(struct mac_addr addr, struct byte_view frame);

// Perform an ARP scan by broadcasting an ARP REQUEST for the device with IPv4 address `dest_ip` from each of the
// registered network devices. `src_ip` is the IPv4 address of this computer (the host) and `dest_ip` is the IPv4
// address that we want to know the MAC address for. `arn` is used for temporary storage.
struct result netdev_arp_scan(struct ipv4_addr src_ip, struct ipv4_addr dest_ip, struct arena arn);

// Register a `struct netdev` network device with the `netdev` subsystem. The `mac_addr` field in the given structure
// `dev` will be used to look up the device. `send_frame` will be called to send a frame over the device. `private_data`
// could, for example, be the driver-specific structure of a network driver that it needs to function.
//
// This function should be called by network device drivers to make themselves available for sending frames after they
// have initialized.
//
// NOTE: The `netdev` subsystem doesn't provide a mechanism for receiving data (at this point in time at least).
// The way to receive data is for the driver to call directly into the Ethernet implementation (or, if there are
// other data link layer protocols implemented in the future, the respective drivers could call directly into the
// respective implementations).
struct result netdev_register_device(struct netdev dev);

#endif // __TX_NETDEV_H__
