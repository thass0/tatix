#ifndef __TX_VFS_H__
#define __TX_VFS_H__

// VFS (Virtual File System) implementation. This is closely modeled after
// the VFS in Linux as far as I understand it. The goal is to provide a
// generic interface to access different file systems, devices, etc.
//
// The design is simple and notably doesn't feature symbolic links and
// mount points yet. Lot's of stuff will be added when features requiring
// it are implemented.
//
// We're sticking to the classic nomencalture here. A `super_block` contains
// control information for a specific file system. It can be though of as
// describing one sub-file system in the VFS.
//
// A `vnode` is what Linux calls an `inode`, although the term `vnode` (for
// "virtual node") is more appropriate and also used commonly. A `vnode` stores
// control information for a specific file in a file system. It's used for
// lookups in the file systems, as well as reads/writes and any other essential
// operations on a file in a file system. Since each `vnode` belongs to a specific
// file system, `vnode`s are allocated from a pool in the appropriate `super_block`.
// Both `super_block`s and `vnode`s are unique globally.
//
// A `dentry` (short for directory entry) represents a single component in a path.
// `dentry`s connect path components to `vnodes` and are used in path lookups. They
// are also unique globally and allocated from a common pool.
//
// A `file` represents the view of a file that a process has after opening it. The
// file itself is unique, but each process has its own independent view. `file`s
// are created through the `open()` system call. They are stored in the process
// structure as they are unique to a process.

// TODO(sync): Protect global data.

#include <tx/base.h>
#include <tx/error.h>
#include <tx/pool.h>
#include <tx/string.h>

struct super_block;
struct vnode;
struct vnode_ops;
struct dentry;
struct file;

///////////////////////////////////////////////////////////////////////////////
// Super block structure(s)                                                  //
///////////////////////////////////////////////////////////////////////////////

struct super_block {
    struct pool vnode_alloc;
    void *private_data;
    struct super_block_ops *ops;
    struct vnode_ops *vnode_ops;
    struct dentry *mount_point;
};

typedef struct vnode *(*super_block_alloc_op)(struct super_block *sb);
typedef void (*super_block_free_op)(struct super_block *sb, struct vnode *vnode);

struct super_block_ops {
    super_block_alloc_op alloc_vnode;
    super_block_free_op free_vnode;
};

///////////////////////////////////////////////////////////////////////////////
// Vnode structure(s)                                                        //
///////////////////////////////////////////////////////////////////////////////

struct vnode {
    i32 refcount;
    struct super_block *sb; // Super block owning this vnode.
    struct {
        sz file_size_bytes;
        // TODO: Add access times when timers are implemented.
    } metadata;
    struct vnode_ops *ops;
};

typedef struct vnode *(*vnode_create_file_op)(struct vnode *parent, struct dentry *filename);
typedef struct vnode *(*vnode_create_dir_op)(struct vnode *parent, struct dentry *filename);
// Requires the super block pointer for looking up the root (as, in that case, `parent` is `NULL`).
typedef struct dentry *(*vnode_lookup_dentry_op)(struct super_block *sb, struct vnode *parent, struct str filename);

struct vnode_ops {
    vnode_create_file_op create_file;
    vnode_create_dir_op create_dir;
    vnode_lookup_dentry_op lookup_dentry;
    // TODO: Require more ops for more features ...
};

///////////////////////////////////////////////////////////////////////////////
// Dentry structure(s)                                                       //
///////////////////////////////////////////////////////////////////////////////

// NOTE: The terms "filename" or "component" are used to refer to a single component
// in a path. A "path", "pathanme" or "filepath" is the entire string of slash-separated
// components that identify a file in the file system.

#define VFS_PATH_MAX_LEN 1024

struct dentry_filename {
    sz len;
    char dat[VFS_PATH_MAX_LEN];
};

struct dentry {
    i32 refcount;
    struct dcache *cache; // Cache of dentrys containing this one
    struct super_block *sb; // Super block owning the vnode of this dentry.
    struct dentry_filename filename; // Name of the component.
    struct dentry *parent; // Pointer to parent directory.
    struct vnode *vnode; // Vnode referred to by this dentry.
    struct dentry_ops *ops;
};

typedef bool (*dentry_compare_op)(struct dentry *a, struct dentry *b);

struct dentry_ops {
    dentry_compare_op compare;
};

///////////////////////////////////////////////////////////////////////////////
// File structure(s)                                                         //
///////////////////////////////////////////////////////////////////////////////

struct file {
    struct dentry *filename;
    sz offset; // Byte offset into the file for reading and writing.
    struct file_ops *ops;
};

typedef sz (*file_seek_op)(struct file *file, sz seek);
typedef struct result_sz (*file_read_op)(struct file *file, struct str_buf sbuf);
typedef struct result_sz (*file_write_op)(struct file *file, struct str str);
typedef struct file *(*file_open_op)(struct dentry *root, struct str path);
typedef struct file *(*file_close_op)(struct file *file);

struct file_ops {
    file_seek_op seek;
    file_read_op read;
    file_write_op write;
    file_open_op open;
    file_close_op close;
};

// Mounting means that after mounting, looking up the mount directory will
// return the root of the mounted filesystem

///////////////////////////////////////////////////////////////////////////////
// VFS functions                                                             //
///////////////////////////////////////////////////////////////////////////////

// Allocate a new super block for a filesystem with private data `data`. Uses the default
// operations if `ops` is `NULL`. Returns `NULL` in case of an error.
struct super_block *vfs_init_sb(void *data, struct super_block_ops *ops, struct vnode_ops *node_ops);

// Mount the super_block at a given dentry.
struct result vfs_mount(struct super_block *sb);

// These use the global dcache.
void vfs_dcache_insert(struct dentry *dentry);
struct dentry *vfs_dcache_lookup(struct str filename);
// Each `dentry` can have different ops, depending on the underlying filesystem.
struct dentry *vfs_dcache_alloc(struct dentry_ops *ops);

#endif // __TX_VFS_H__
