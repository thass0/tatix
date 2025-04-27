#ifndef __TX_IP_H__
#define __TX_IP_H__

#include <tx/base.h>
#include <tx/error.h>
#include <tx/netorder.h>

struct ip_addr {
    u8 addr[4];
} __packed;

static_assert(sizeof(struct ip_addr) == 4);

struct_result(ip_addr, struct ip_addr);

static inline struct ip_addr ip_addr(u8 a0, u8 a1, u8 a2, u8 a3)
{
    struct ip_addr addr;
    addr.addr[0] = a0;
    addr.addr[1] = a1;
    addr.addr[2] = a2;
    addr.addr[3] = a3;
    return addr;
}

static inline bool ip_addr_is_equal(struct ip_addr addr1, struct ip_addr addr2)
{
    return addr1.addr == addr2.addr;
}

#endif // __TX_IP_H__
