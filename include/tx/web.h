// Static site web server

#ifndef __TX_WEB_H__
#define __TX_WEB_H__

#include <tx/base.h>
#include <tx/net/ip_addr.h>
#include <tx/ramfs.h>

// Listen to web requests and server the content in `root`.
struct result web_listen(struct ipv4_addr ip_addr, u16 port, struct ram_fs_node *root);

#endif // __TX_WEB_H__
