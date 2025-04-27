// Generic network device interface.

#ifndef __TX_NETDEV_H__
#define __TX_NETDEV_H__

#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/ethernet.h>
#include <tx/ip.h>

struct netdev;

typedef struct result (*send_frame_func_t)(struct netdev *dev, struct byte_view frame);

struct netdev {
    struct mac_addr mac_addr;
    send_frame_func_t send_frame;
    void *private_data;
};

struct result netdev_send_frame(struct mac_addr addr, struct byte_view frame);

struct result netdev_arp_scan(struct ip_addr src_ip, struct ip_addr dest_ip, struct arena arn);

struct result netdev_register_device(struct netdev dev);

#endif // __TX_NETDEV_H__
