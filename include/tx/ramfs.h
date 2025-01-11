// A simple in-RAM file system.

#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/error.h>
#include <tx/pool.h>
#include <tx/string.h>

#define RAM_FS_MAX_NODES_NUM 1024
#define RAM_FS_MAX_BYTES_NUM BIT(20) /* 1 MiB */

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
    struct bytes data;
};

struct_result(ram_fs_node, struct ram_fs_node *)

struct ram_fs {
    // TODO: The memory allocation strategy here is terrible.
    struct buddy *data_alloc;
    struct pool *node_alloc;
    struct arena string_alloc;
    struct ram_fs_node *root;
};

struct ram_fs ram_fs_new(struct buddy *alloc, struct arena *arn);
struct result_ram_fs_node ram_fs_create_dir(struct ram_fs *rfs, struct str dirname, struct arena scratch);
struct result_ram_fs_node ram_fs_create_file(struct ram_fs *rfs, struct str filename, struct arena scratch);
struct result_ram_fs_node ram_fs_open(struct ram_fs *rfs, struct str filename, struct arena scratch);
struct result_sz ram_fs_read(struct ram_fs_node *rfs_node, struct str_buf sbuf, sz offset);
struct result_sz ram_fs_write(struct ram_fs_node *rfs_node, struct str str, sz offset);

void ram_fs_run_tests(struct arena arn);
