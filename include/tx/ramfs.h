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
    struct byte_buf data;
    // Pointer back to parent FS.
    struct ram_fs *fs;
};

struct_result(ram_fs_node, struct ram_fs_node *);

struct ram_fs {
    // TODO: A flexible file system would require the node_alloc and string_alloc to grow dynamically.
    struct alloc data_alloc;
    struct pool node_alloc;
    struct arena scratch;
    struct ram_fs_node *root;
};

// Create a new RAM fs instance. `alloc` will be the primary source of memory used to store
// nodes and data. Returns `NULL` if it failed to set up the RAM fs instance.
struct ram_fs *ram_fs_new(struct alloc alloc);

// Create an empty directory in the `root` node. `dirname` is the path to the new directory. If `recursive` is
// `true`, missing parent directories will be created automatically. Returns the node of the new directory or an error.
struct result_ram_fs_node ram_fs_create_dir(struct ram_fs_node *root, struct str dirname, bool recursive);

// Create an empty file in the `root` node. `filename` is the path to the new file. If `recursive` is `true`, missing
// parent directories will be created automatically. Returns the node of the new file or an error. This node can
// directly be used to write to or read from the file, without the need to open it again.
struct result_ram_fs_node ram_fs_create_file(struct ram_fs_node *root, struct str filename, bool recursive);

// Open the file under the path `filename` relative to `node`. Returns the node of the file or an error.
struct result_ram_fs_node ram_fs_open(struct ram_fs_node *root, struct str filename);

// Read from the file behind `rfs_node`. Data is read into `bbuf` starting at `offset` until `bbuf`
// is full or the end of the file is reached. Returns the number of bytes read or an error.
struct result_sz ram_fs_read(struct ram_fs_node *rfs_node, struct byte_buf *bbuf, sz offset);

// Write to the file behind `rfs_node`. Data is written into the file from `bview` starting at `offset`.
// Existing data at `offset` will be overwritten. If `offset` is equal to the length of the file, the
// data from `bview` will be appended. Returns the number of bytes written or an error.
struct result_sz ram_fs_write(struct ram_fs_node *rfs_node, struct byte_view bview, sz offset);

void ram_fs_run_tests(struct arena arn);

#endif // __TX_RAMFS_H__
