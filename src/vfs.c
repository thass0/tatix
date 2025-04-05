#include <tx/assert.h>
#include <tx/kvalloc.h>
#include <tx/pool.h>
#include <tx/vfs.h>

struct dcache {
    struct pool dentry_alloc;
    // TODO: Replace with hash map:
    sz dentry_ptrs_len; // Length of list of pointers to dentrys.
    struct dentry **dentry_ptrs; // List of pointers to all dentrys in the cache. Unused entries are `NULL`.
};

static struct dcache global_dcache;
static bool global_dcache_is_initialized = false;

///////////////////////////////////////////////////////////////////////////////
// Super block functions                                                     //
///////////////////////////////////////////////////////////////////////////////

static struct vnode *sb_alloc_vnode_default(struct super_block *sb)
{
    assert(sb);

    struct vnode *vnode = pool_alloc(&sb->vnode_alloc);
    if (!vnode)
        return NULL;

    vnode->refcount = 1;
    vnode->sb = sb;
    vnode->metadata.file_size_bytes = 0;
    vnode->ops = sb->vnode_ops;

    return vnode;
}

static void sb_free_vnode_default(struct super_block *sb, struct vnode *vnode)
{
    assert(sb);
    if (vnode)
        assert(!vnode->refcount);
    // NOTE: `vnode->ops` don't need to be free'd as they are not owned by the VFS
    // subsystem, but by the implementation of a specific FS.
    pool_free(&sb->vnode_alloc, vnode);
}

///////////////////////////////////////////////////////////////////////////////
// Dcache and dentry functions                                               //
///////////////////////////////////////////////////////////////////////////////

#define DENTRY_FILENAME_EMPTY ((struct dentry_filename){ 0 })

static struct result dcache_init_global(void)
{
    assert(!global_dcache_is_initialized);

    sz pool_mem_size = sizeof(struct dentry) * 1024;
    void *pool_mem = kvalloc_alloc(pool_mem_size, alignof(struct dentry));
    if (!pool_mem)
        return result_error(ENOMEM);
    global_dcache.dentry_alloc = pool_new(bytes_new(pool_mem, pool_mem_size), sizeof(struct dentry));

    sz dentry_ptrs_len = 1024;
    struct dentry **dentry_ptrs = kvalloc_alloc(dentry_ptrs_len * sizeof(struct dentry *), alignof(struct dentry *));
    if (!dentry_ptrs) {
        kvalloc_free(pool_mem, pool_mem_size);
        return result_error(ENOMEM);
    }
    global_dcache.dentry_ptrs_len = dentry_ptrs_len;
    global_dcache.dentry_ptrs = dentry_ptrs;

    global_dcache_is_initialized = true;

    return result_ok();
}

struct dentry *vfs_dcache_new(struct dentry_ops *ops)
{
    assert(global_dcache_is_initialized);
    assert(ops);

    struct dentry *dentry = pool_alloc(&global_dcache.dentry_alloc);
    if (!dentry)
        return NULL;

    dentry->refcount = 1;
    dentry->cache = &global_dcache;
    dentry->sb = NULL;
    dentry->filename = DENTRY_FILENAME_EMPTY;
    dentry->parent = NULL;
    dentry->vnode = NULL;
    dentry->ops = ops;

    return dentry;
}

///////////////////////////////////////////////////////////////////////////////
// VFS functions                                                             //
///////////////////////////////////////////////////////////////////////////////

struct super_block *vfs_init_sb(void *data, struct super_block_ops *ops, struct vnode_ops *vnode_ops)
{
    assert(data);
    assert(vnode_ops);

    struct super_block *sb = kvalloc_alloc(sizeof(*sb), alignof(*sb));
    if (!sb)
        goto free_nothing;

    sz sb_pool_mem_size = sizeof(struct vnode) * 512;
    void *sb_pool_mem = kvalloc_alloc(sb_pool_mem_size, alignof(struct vnode));
    if (!sb_pool_mem)
        goto free_sb;
    sb->vnode_alloc = pool_new(bytes_new(sb_pool_mem, sb_pool_mem_size), sizeof(struct vnode));

    sb->private_data = data;

    if (!ops) {
        ops = kvalloc_alloc(sizeof(*ops), alignof(*ops));
        if (!ops)
            goto free_pool_mem;
        ops->alloc_vnode = sb_alloc_vnode_default;
        ops->free_vnode = sb_free_vnode_default;
    }
    sb->ops = ops;

    sb->vnode_ops = vnode_ops;

    return sb;

free_pool_mem:
    kvalloc_free(sb_pool_mem, sb_pool_mem_size);
free_sb:
    kvalloc_free(sb, sizeof(*sb));
free_nothing:
    return NULL;
}

struct result vfs_mount(struct super_block *sb, struct str mount_path, struct vnode *vnode)
{
    assert(sb);

    if (!global_dcache_is_initialized) {
        struct result res = dcache_init_global();
        if (res.is_error)
            return res;
    }

    struct dentry *mount_dentry = vfs_dcache_lookup(mount_path);
    if (!mount_dentry)
        return result_error(ENOENT);

    mount_dentry->vnode = vnode;
    // TODO: In einem super_block werden alle dentry ops gleich sein.
    mount_dentry->ops = sb->

    return result_ok();
}
