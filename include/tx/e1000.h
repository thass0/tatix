#ifndef __TX_E1000_H__
#define __TX_E1000_H__

#include <tx/byte.h>
#include <tx/error.h>
#include <tx/ethernet.h>

// This is temporary until there is a proper netdev implementation.

#define E1000_TX_BUF_SIZE 2048
struct result e1000_send(struct byte_view frame);
struct result_mac_addr e1000_mac(void);

#endif // __TX_E1000_H__
