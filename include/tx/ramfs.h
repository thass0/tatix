// A simple in-RAM file system.

#ifndef __TX_RAMFS_H__
#define __TX_RAMFS_H__

#include <config.h>
#include <tx/alloc.h>
#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/error.h>
#include <tx/pool.h>
#include <tx/string.h>

#define RAM_FS_MAX_NODES_NUM 10000
#define RAM_FS_DEFAULT_FILE_SIZE PAGE_SIZE

enum ram_fs_node_type {
    RAM_FS_TYPE_FILE,
    RAM_FS_TYPE_DIR,
};

struct ram_fs_node {
    // First node in the directory if this node is of type RAM_FS_TYPE_DIR.
    struct ram_fs_node *first;
    // Next node in the same directory as this node. A linked list.
    struct ram_fs_node *next;
    enum ram_fs_node_type type;
    struct str name;
    // Data of the file if this node is of type RAM_FS_TYPE_FILE.
    struct str_buf data;
    // Pointer back to parent FS.
    struct ram_fs *fs;
};

struct_result(ram_fs_node, struct ram_fs_node *)

struct ram_fs {
    // TODO: A flexible file system would require the node_alloc and string_alloc to grow dynamically.
    struct alloc data_alloc;
    struct pool node_alloc;
    struct arena scratch;
    struct ram_fs_node *root;
};

struct ram_fs *ram_fs_new(struct alloc alloc);
struct result_ram_fs_node ram_fs_create_dir(struct ram_fs *rfs, struct str dirname);
struct result_ram_fs_node ram_fs_create_file(struct ram_fs *rfs, struct str filename);
struct result_ram_fs_node ram_fs_open(struct ram_fs *rfs, struct str filename);
struct result_sz ram_fs_read(struct ram_fs_node *rfs_node, struct str_buf *sbuf, sz offset);
struct result_sz ram_fs_write(struct ram_fs_node *rfs_node, struct str str, sz offset);

void ram_fs_run_tests(struct arena arn);

#endif // __TX_RAMFS_H__
