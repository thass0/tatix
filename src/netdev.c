#include <tx/arp.h>
#include <tx/netdev.h>
#include <tx/print.h>

#define NETDEV_TABLE_SIZE 16
static bool global_netdev_table_used[NETDEV_TABLE_SIZE];
static struct netdev global_netdev_table[NETDEV_TABLE_SIZE];

struct result netdev_register_device(struct netdev dev)
{
    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i] && mac_addr_is_equal(dev.mac_addr, global_netdev_table[i].mac_addr)) {
            print_dbg(PDBG, STR("Device with MAC address %hhx:%hhx:%hhx:%hhx:%hhx:%hhx already exists\n"),
                      dev.mac_addr.addr[0], dev.mac_addr.addr[1], dev.mac_addr.addr[2], dev.mac_addr.addr[3],
                      dev.mac_addr.addr[4], dev.mac_addr.addr[5]);
            return result_error(EEXIST);
        }
    }

    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (!global_netdev_table_used[i]) {
            global_netdev_table_used[i] = true;
            global_netdev_table[i] = dev;
            print_dbg(PINFO, STR("Registered device with MAC address %hhx:%hhx:%hhx:%hhx:%hhx:%hhx\n"),
                      dev.mac_addr.addr[0], dev.mac_addr.addr[1], dev.mac_addr.addr[2], dev.mac_addr.addr[3],
                      dev.mac_addr.addr[4], dev.mac_addr.addr[5]);
            return result_ok();
        }
    }

    return result_error(ENOMEM);
}

static struct netdev *netdev_lookup_mac_addr(struct mac_addr addr)
{
    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i] && mac_addr_is_equal(addr, global_netdev_table[i].mac_addr)) {
            return &global_netdev_table[i];
        }
    }

    return NULL;
}

struct result netdev_send_frame(struct mac_addr addr, struct byte_view frame)
{
    struct netdev *dev = netdev_lookup_mac_addr(addr);
    if (!dev)
        return result_error(ENODEV);

    assert(dev->send_frame);
    return dev->send_frame(dev, frame);
}

struct result netdev_arp_scan(struct ip_addr src_ip, struct ip_addr dest_ip, struct arena arn)
{
    struct result res = result_ok();

    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i]) {
            struct netdev *dev = &global_netdev_table[i];
            res = arp_send_request(src_ip, dev->mac_addr, dest_ip, arn);
            if (res.is_error)
                return res;
        }
    }

    return result_ok();
}
