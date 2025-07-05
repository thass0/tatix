// Static site web server

#ifndef __TX_WEB_H__
#define __TX_WEB_H__

#include <tx/base.h>
#include <tx/net/ip_addr.h>
#include <tx/ramfs.h>

// Listen to web requests and server the content in `root`. `compressed` is a directory containing gzip-compressed
// versions of big files in `root`. The file names in `compressed` must the identical to those in `root`.
struct result web_listen(struct ipv4_addr ip_addr, u16 port, struct ram_fs_node *root, struct ram_fs_node *compressed);

#endif // __TX_WEB_H__
