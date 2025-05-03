// Generic network device interface.

#ifndef __TX_NETDEV_H__
#define __TX_NETDEV_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/ip.h>
#include <tx/mac.h>

struct netdev;

typedef struct result (*send_frame_func_t)(struct netdev *dev, struct byte_view frame);

struct netdev {
    struct mac_addr mac_addr;
    struct ipv4_addr ip_addr;
    send_frame_func_t send_frame;
    void *private_data;
};

// Set the `netdev` subsystem with a default IP address to use for all new devices.
void netdev_set_default_ip_addr(struct ipv4_addr ip_addr);

// Send a frame (only Ethernet right now) with data `frame` using the device with MAC address `addr`.
struct result netdev_send_frame(struct mac_addr addr, struct byte_view frame);

// Perform an ARP scan by broadcasting an ARP request for the device with IPv4 address `ip_addr` from each of the
// registered network devices. `ip_addr` is the IPv4 address that we want to know the MAC address for. The IP addresses
// assigned to the individual network devices will be used as the source IP addresses in the ARP requests. `arn` is
// used for temporary storage.
struct result netdev_arp_scan(struct ipv4_addr ip_addr, struct arena arn);

// Register a `struct netdev` network device with the `netdev` subsystem. The `mac_addr` field in the given structure
// `dev` will be used to look up the device. `send_frame` will be called to send a frame over the device. `private_data`
// could, for example, be the driver-specific structure of a network driver that it needs to function. The `ip_addr`
// field must be 0.0.0.0, indicating that the default IP address can be assigned.
//
// The memory behind the `dev` pointer passed to this function is owned by the driver.
//
// This function should be called by network device drivers to make themselves available for sending frames after they
// have initialized.
//
// NOTE: The `netdev` subsystem doesn't provide a mechanism for receiving data (at this point in time at least).
// The way to receive data is for the driver to call directly into the Ethernet implementation (or, if there are
// other data link layer protocols implemented in the future, the respective drivers could call directly into the
// respective implementations).
struct result netdev_register_device(struct netdev *dev);

#endif // __TX_NETDEV_H__
