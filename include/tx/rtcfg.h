// Runtime configuration

#ifndef __TX_RTCFG_H__
#define __TX_RTCFG_H__

#include <tx/arena.h>
#include <tx/error.h>
#include <tx/net/ip_addr.h>
#include <tx/ramfs.h>
#include <tx/string.h>

struct runtime_config {
    struct option_ipv4_addr host_ip;
    struct option_ipv4_addr local_ip;
    struct option_ipv4_addr local_ip_mask;
    struct option_ipv4_addr default_gateway_ip;
};

struct_result(runtime_config, struct runtime_config *);

struct result_runtime_config rtcfg_read_config(struct ram_fs *rfs, struct str cfg_filename, struct arena arn);

#endif // __TX_RTCFG_H__
