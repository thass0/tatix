#include <tx/arp.h>
#include <tx/netdev.h>
#include <tx/print.h>

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

static struct netdev *netdev_lookup_mac_addr(struct mac_addr addr)
{
    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i] && mac_addr_is_equal(addr, global_netdev_table[i]->mac_addr)) {
            return global_netdev_table[i];
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

struct result netdev_arp_scan(struct ipv4_addr ip_addr, struct arena arn)
{
    struct result res = result_ok();

    for (sz i = 0; i < NETDEV_TABLE_SIZE; i++) {
        if (global_netdev_table_used[i]) {
            struct netdev *dev = global_netdev_table[i];
            res = arp_send_request(dev->ip_addr, dev->mac_addr, ip_addr, arn);
            if (res.is_error)
                return res;
        }
    }

    return result_ok();
}
