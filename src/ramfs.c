#include <tx/arena.h>
#include <tx/base.h>
#include <tx/error.h>
#include <tx/fmt.h>
#include <tx/ramfs.h>

///////////////////////////////////////////////////////////////////////////////
// Paths                                                                     //
///////////////////////////////////////////////////////////////////////////////

#define PATH_NAME_MAX_LEN 0x1000

struct path_name {
    // `src` holds a copy of the original path name with minimal modifications. The `components`
    // array contains string slices pointing into `src`. Each of these slices represents one component
    // of a path without '/' characters.
    struct str src;
    // The path '/' is represented by a `struct path_name` where `n_components` is 0, the empty path.
    sz n_components;
    struct str *components;
    bool is_absolute;
};

struct_result(path_name, struct path_name);

// Parse a path name into a `struct path_name`. Uses `arn` to allocate memory for the fields in
// the returned `struct path_name`. Returns an error if the path is invalid or too long.
// The empty path (`n_components == 0`) represents the path '/'.
static struct result_path_name path_name_parse(struct str name, struct arena *arn)
{
    if (name.len == 0)
        return result_path_name_error(EINVAL);

    if (name.len > PATH_NAME_MAX_LEN)
        return result_path_name_error(ENAMETOOLONG);

    // TODO: Implement relative paths
    assert(name.dat[0] == '/');
    name.dat++;
    name.len--;

    // We can forget the trailing '/' as it's ignored anyway.
    if (name.len > 0 && name.dat[name.len - 1] == '/')
        name.len--;

    // The '\0' character is commonly forbidden in paths.
    if (!str_find_char(name, '\0').is_none)
        return result_path_name_error(EINVAL);

    struct path_name path;
    path.is_absolute = true; // TODO: Implement relative paths

    // `struct path_name` needs to maintain a copy of the name string so that the string slices
    // in `components` can point to it.
    struct str_buf src_buf = str_buf_from_arena(arn, name.len);
    append_str(name, &src_buf);
    path.src = str_from_buf(src_buf);

    // A path of length N can at most have C = N / 2 + 1 components. The reasoning goes as follows. A path with
    // two components (C = 2) of the minimum component length of one contains at least one '/' character. So the
    // path length is three (N = 3) and C = N / 2 + 1 = 3 / 2 + 1 = 2 gives the correct number of components. To
    // add one component (C' = C + 1) of minimum length to the path, one more '/' is required to separate the new
    // component from the existing ones. So the new path length is N' = N + 2 and
    //
    //   C' = N' / 2 + 1 = (N + 2) / 2 + 1 = N / 2 + 1 + 1 = C + 1
    //
    // gives the correct number of components.
    sz max_n_components = name.len / 2 + 1;
    path.components =
        arena_alloc_aligned_array(arn, max_n_components, sizeof(*path.components), alignof(*path.components));
    path.n_components = 0;

    struct option_sz opt_idx;
    struct str curr;
    struct str src = path.src;

    while (src.len) {
        // There can't be any more components than `max_n_components` (see above).
        assert(path.n_components < max_n_components);

        opt_idx = str_find_char(src, '/');

        if (opt_idx.is_none) {
            curr = str_new(src.dat, src.len);
            src.len = 0;
        } else {
            sz next_len = option_sz_checked(opt_idx);

            if (next_len == 0) {
                // There was another '/' character after the last one. We ignore it and continue.
                src.len--;
                src.dat++;
                continue;
            }

            curr = str_new(src.dat, next_len);
            // We need to add 1 to skip the '/' character.
            src.len -= next_len + 1;
            src.dat += next_len + 1;
        }

        path.components[path.n_components++] = curr;
    }

    return result_path_name_ok(path);
}

struct str path_name_to_str(struct path_name path_name, struct arena *arn)
{
    struct str_buf sbuf = str_buf_from_arena(arn, PATH_NAME_MAX_LEN);
    if (path_name.is_absolute)
        append_char('/', &sbuf);
    for (sz i = 0; i < path_name.n_components; i++) {
        if (i)
            append_char('/', &sbuf);
        append_str(path_name.components[i], &sbuf);
    }
    return str_from_buf(sbuf);
}

///////////////////////////////////////////////////////////////////////////////
// Node lookup                                                               //
///////////////////////////////////////////////////////////////////////////////

// Lookup the node for `path` starting at `curr`. `path` must be non-empty. Returns `NULL`
// if no suitable node was found or `curr` is `NULL`.
static struct ram_fs_node *ram_fs_node_lookup_at(struct ram_fs_node *curr, struct path_name path)
{
    assert(path.n_components != 0);

    sz i = 0;

    while (curr) {
        if (str_is_equal(curr->name, path.components[i])) {
            if (i + 1 == path.n_components)
                return curr;

            switch (curr->type) {
            case RAM_FS_TYPE_DIR:
                curr = curr->first;
                i++;
                break;
            case RAM_FS_TYPE_FILE:
                // A file was reached without reaching the end of the path so the path doesn't exist.
                return NULL;
            }
        } else {
            // Go to the next entry in the current directory
            curr = curr->next;
        }
    }

    return NULL;
}

// Lookup the node for `path` in the given RAM FS `rfs`.
static struct ram_fs_node *ram_fs_node_lookup(struct ram_fs *rfs, struct path_name path)
{
    assert(rfs);

    struct ram_fs_node *orig = NULL;

    if (path.is_absolute) {
        orig = rfs->root;

        // Special case for the root directory
        if (path.n_components == 0)
            return orig;

        return ram_fs_node_lookup_at(orig->first, path);
    } else {
        assert(false); // TODO: Implement relative paths
    }
}

///////////////////////////////////////////////////////////////////////////////
// Node creation                                                             //
///////////////////////////////////////////////////////////////////////////////

struct ram_fs_node *ram_fs_node_alloc(struct ram_fs *rfs, struct str name, enum ram_fs_node_type type)
{
    struct ram_fs_node *node = pool_alloc(&rfs->node_alloc);
    if (!node)
        return NULL;
    node->first = NULL;
    node->next = NULL;
    node->type = type;
    node->fs = rfs;

    struct str_buf name_buf;
    name_buf.dat = alloc_alloc(rfs->data_alloc, name.len, alignof(void *));
    name_buf.len = 0;
    name_buf.cap = name.len;
    append_str(name, &name_buf);
    node->name = str_from_buf(name_buf);
    node->data = str_buf_new(NULL, 0, 0);
    return node;
}

static struct result_ram_fs_node ram_fs_create_common(struct ram_fs *rfs, struct str nodepath,
                                                      enum ram_fs_node_type type, bool recursive, struct arena scratch)
{
    // NOTE: This function calls itself recursively when creating directories recursively.
    // So be careful to not reset the scratch arena in the recursive call. Otherwise, existing
    // memory allocations will be messed up.

    struct result_path_name path_res = path_name_parse(nodepath, &scratch);
    if (path_res.is_error)
        return result_ram_fs_node_error(path_res.code);

    struct path_name path = result_path_name_checked(path_res);
    if (path.n_components == 0) {
        // Path was '/'. The root is created along with the file system and must already exist.
        return result_ram_fs_node_error(EEXIST);
    }

    assert(path.is_absolute); // TODO: Verify that lookups also work once relative paths are implemented.
    struct str nodename = path.components[path.n_components - 1]; // - 1 is OK because path isn't root.

    // For parent lookup, we need to ignore the name of the new node.
    path.n_components--;
    struct ram_fs_node *parent = ram_fs_node_lookup(rfs, path);

    if (!parent) {
        if (!recursive) {
            // Parent directory doesn't exist so this node can't be created.
            return result_ram_fs_node_error(ENOENT);
        }

        struct str parent_path = path_name_to_str(path, &scratch);
        print_dbg(STR("parent_path: '%s'\n"), parent_path);
        struct result_ram_fs_node parent_res = ram_fs_create_common(rfs, parent_path, RAM_FS_TYPE_DIR, true, scratch);
        if (parent_res.is_error)
            return parent_res;
        parent = result_ram_fs_node_checked(parent_res);
    }

    if (parent->type != RAM_FS_TYPE_DIR) {
        // We can't add a node to a file.
        return result_ram_fs_node_error(ENOTDIR);
    }

    // Look for conflicts
    if (parent->first) {
        struct ram_fs_node *curr = parent->first;
        while (curr) {
            if (str_is_equal(curr->name, nodename)) {
                return result_ram_fs_node_error(EEXIST);
            }
            curr = curr->next;
        }
    }

    struct ram_fs_node *node = ram_fs_node_alloc(rfs, nodename, type);
    if (!node)
        return result_ram_fs_node_error(ENOMEM);

    if (parent->first) {
        // Append the new node to the list of nodes in the parent directory.
        struct ram_fs_node *curr = parent->first;
        while (curr->next)
            curr = curr->next;
        curr->next = node;
    } else {
        // Make the new node the first entry in the parent directory.
        parent->first = node;
    }

    return result_ram_fs_node_ok(node);
}

///////////////////////////////////////////////////////////////////////////////
// Public functions                                                          //
///////////////////////////////////////////////////////////////////////////////

struct ram_fs *ram_fs_new(struct alloc alloc)
{
    struct ram_fs *rfs;

    rfs = alloc_alloc(alloc, sizeof(*rfs), alignof(*rfs));
    if (!rfs)
        return NULL;

    sz node_mem_size = RAM_FS_MAX_NODES_NUM * sizeof(struct ram_fs_node);
    void *node_mem = alloc_alloc(alloc, node_mem_size, alignof(struct ram_fs_node));
    if (!node_mem)
        return NULL;
    rfs->node_alloc = pool_new(bytes_new(node_mem, node_mem_size), sizeof(struct ram_fs_node));

    sz scratch_mem_size = 4 * PATH_NAME_MAX_LEN;
    void *scratch_mem = alloc_alloc(alloc, scratch_mem_size, alignof(void *));
    if (!scratch_mem)
        return NULL;
    rfs->scratch = arena_new(bytes_new(scratch_mem, scratch_mem_size));

    rfs->data_alloc = alloc;

    // The root must exists from the beginning as `ram_fs_create_common` needs it but can't create it itself.
    struct ram_fs_node *root_dir = pool_alloc(&rfs->node_alloc);
    assert(root_dir); // We just created the pool so there's surely enough memory.
    root_dir->first = NULL;
    root_dir->next = NULL;
    root_dir->type = RAM_FS_TYPE_DIR;
    root_dir->name = STR("");
    root_dir->data = str_buf_new(NULL, 0, 0);
    root_dir->fs = rfs;

    rfs->root = root_dir;

    return rfs;
}

struct result_ram_fs_node ram_fs_create_dir(struct ram_fs *rfs, struct str dirpath, bool recursive)
{
    assert(rfs);
    return ram_fs_create_common(rfs, dirpath, RAM_FS_TYPE_DIR, recursive, rfs->scratch);
}

struct result_ram_fs_node ram_fs_create_file(struct ram_fs *rfs, struct str filepath, bool recursive)
{
    assert(rfs);
    struct result_ram_fs_node node_res = ram_fs_create_common(rfs, filepath, RAM_FS_TYPE_FILE, recursive, rfs->scratch);
    if (node_res.is_error)
        return node_res;
    struct ram_fs_node *node = result_ram_fs_node_checked(node_res);
    void *data = alloc_alloc(rfs->data_alloc, RAM_FS_DEFAULT_FILE_SIZE, alignof(void *));
    if (!data)
        return result_ram_fs_node_error(ENOMEM);
    node->data = str_buf_new(data, 0, RAM_FS_DEFAULT_FILE_SIZE);
    return result_ram_fs_node_ok(node);
}

struct result_ram_fs_node ram_fs_open(struct ram_fs *rfs, struct str filename)
{
    assert(rfs);

    struct arena scratch = rfs->scratch;
    struct result_path_name path_res = path_name_parse(filename, &scratch);
    if (path_res.is_error)
        return result_ram_fs_node_error(path_res.code);

    struct path_name path = result_path_name_checked(path_res);

    if (path.n_components == 0)
        return result_ram_fs_node_ok(rfs->root);

    struct ram_fs_node *node = ram_fs_node_lookup(rfs, path);
    if (!node)
        return result_ram_fs_node_error(ENOENT);

    return result_ram_fs_node_ok(node);
}

struct result_sz ram_fs_read(struct ram_fs_node *rfs_node, struct str_buf *sbuf, sz offset)
{
    assert(rfs_node);
    assert(sbuf);

    if (rfs_node->type != RAM_FS_TYPE_FILE)
        return result_sz_error(EINVAL);

    if (offset > rfs_node->data.len)
        return result_sz_error(EINVAL);
    if (offset == rfs_node->data.len)
        return result_sz_ok(0);

    sz avail = rfs_node->data.len - offset;
    sz read_len = MIN(sbuf->cap, avail);
    struct result res = append_str(str_new((char *)rfs_node->data.dat + offset, read_len), sbuf);

    if (res.is_error)
        return result_sz_error(res.code);
    return result_sz_ok(read_len);
}

struct result_sz ram_fs_write(struct ram_fs_node *rfs_node, struct str str, sz offset)
{
    assert(rfs_node);

    if (rfs_node->type != RAM_FS_TYPE_FILE)
        return result_sz_error(EINVAL);

    // Files are initialized to contain some data when created.
    assert(rfs_node->data.dat != NULL && rfs_node->data.cap != 0);

    // An offset outside of the file doesn't make sense. If the offset is equal to the file length,
    // the write operation appends to the file.
    if (offset > rfs_node->data.len)
        return result_sz_error(EINVAL);

    if (str.len + offset > rfs_node->data.cap) {
        // The write operation exceeds the capacity of the file. We need to reallocate the data.
        sz new_data_cap = 2 * rfs_node->data.cap;
        void *new_data = alloc_alloc(rfs_node->fs->data_alloc, new_data_cap, alignof(void *));
        if (!new_data)
            return result_sz_error(ENOMEM);
        struct str_buf new_data_buf = str_buf_new(new_data, 0, new_data_cap);
        append_str(str_from_buf(rfs_node->data), &new_data_buf);
        rfs_node->data = new_data_buf;
    }

    // Now we have: str.len + offset <= rfs_node->data.cap => str.len <= rfs_node->data.cap - offset
    sz avail = rfs_node->data.cap - offset;
    assert(str.len <= avail); // Make sure the reallocation above worked as expected.
    sz write_len = MIN(str.len, avail);

    // Make sure we don't write of out bounds of the buffer. These assertions may seem redundant, but
    // better safe than sorry.
    assert(rfs_node->data.cap >= offset);
    assert(write_len <= rfs_node->data.cap - offset);
    memcpy(bytes_new((byte *)rfs_node->data.dat + offset, write_len), bytes_from_str(str));
    rfs_node->data.len = MAX(rfs_node->data.len, offset + write_len);

    return result_sz_ok(write_len);
}

///////////////////////////////////////////////////////////////////////////////
// Tests                                                                     //
///////////////////////////////////////////////////////////////////////////////

#define RAM_FS_TEST_SIZE (4 * BIT(20)) /* 4MiB */

static struct alloc test_helper_create_alloc(struct arena *arn)
{
    struct buddy *rfs_data_alloc = buddy_init(bytes_from_arena(RAM_FS_TEST_SIZE, arn), arn);
    return alloc_new(rfs_data_alloc, buddy_alloc_wrapper, buddy_free_wrapper);
}

static void test_path_name_parse(struct arena arn)
{
    struct result_path_name path_name_res;
    struct path_name path_name;

    path_name_res = path_name_parse(STR("/"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == 0);
    assert(str_is_equal(path_name.src, STR("")));

    path_name_res = path_name_parse(STR("/this-is-random-nonsense"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == 1);
    assert(str_is_equal(path_name.src, STR("this-is-random-nonsense")));
    assert(str_is_equal(path_name.components[0], STR("this-is-random-nonsense")));

    // Note the trailing '/' this time
    path_name_res = path_name_parse(STR("/this-is-random-nonsense/"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == 1);
    assert(str_is_equal(path_name.src, STR("this-is-random-nonsense")));
    assert(str_is_equal(path_name.components[0], STR("this-is-random-nonsense")));

    path_name_res = path_name_parse(STR("/foo/bar"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == 2);
    assert(str_is_equal(path_name.src, STR("foo/bar")));
    assert(str_is_equal(path_name.components[0], STR("foo")));
    assert(str_is_equal(path_name.components[1], STR("bar")));

    // Note the trailing '/' this time.
    path_name_res = path_name_parse(STR("/foo/bar/"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == 2);
    assert(str_is_equal(path_name.src, STR("foo/bar")));
    assert(str_is_equal(path_name.components[0], STR("foo")));
    assert(str_is_equal(path_name.components[1], STR("bar")));

    path_name_res = path_name_parse(STR("/foo//bar"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == 2);
    assert(str_is_equal(path_name.src, STR("foo//bar")));
    assert(str_is_equal(path_name.components[0], STR("foo")));
    assert(str_is_equal(path_name.components[1], STR("bar")));

    // The "special" path components '.' and '..' should be treated the same as any path name
    // because they are handled by the lookup routines, not by the path parser.
    path_name_res = path_name_parse(STR("/./blah/../..//.../"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == 5);
    assert(str_is_equal(path_name.src, STR("./blah/../..//...")));
    assert(str_is_equal(path_name.components[0], STR(".")));
    assert(str_is_equal(path_name.components[1], STR("blah")));
    assert(str_is_equal(path_name.components[2], STR("..")));
    assert(str_is_equal(path_name.components[3], STR("..")));
    assert(str_is_equal(path_name.components[4], STR("...")));

    // Error conditions

    path_name_res = path_name_parse(STR(""), &arn);
    assert(path_name_res.is_error);
    assert(path_name_res.code == EINVAL);

    // '\0' character is forbidden.
    path_name_res = path_name_parse(STR("/blah/\0/foo"), &arn);
    assert(path_name_res.is_error);
    assert(path_name_res.code == EINVAL);

    // Maximum length
    struct str_buf sbuf = str_buf_from_arena(&arn, PATH_NAME_MAX_LEN + 2);
    for (i32 i = 0; i < PATH_NAME_MAX_LEN / 2; i++)
        append_str(STR("/a"), &sbuf);
    path_name_res = path_name_parse(str_from_buf(sbuf), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(path_name.n_components == PATH_NAME_MAX_LEN / 2);
    for (i32 i = 0; i < PATH_NAME_MAX_LEN / 2; i++)
        assert(str_is_equal(path_name.components[i], STR("a")));

    // Now we make it too long
    append_str(STR("/a"), &sbuf);
    path_name_res = path_name_parse(str_from_buf(sbuf), &arn);
    assert(path_name_res.is_error);
    assert(path_name_res.code == ENAMETOOLONG);
}

static void test_path_name_to_str(struct arena arn)
{
    struct result_path_name path_name_res;
    struct path_name path_name;

    path_name_res = path_name_parse(STR("/"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(str_is_equal(path_name_to_str(path_name, &arn), STR("/")));

    path_name_res = path_name_parse(STR("/foo/bar"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(str_is_equal(path_name_to_str(path_name, &arn), STR("/foo/bar")));

    path_name_res = path_name_parse(STR("/foo//bar"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(str_is_equal(path_name_to_str(path_name, &arn), STR("/foo/bar")));

    path_name_res = path_name_parse(STR("/./blah/../..//.../"), &arn);
    assert(!path_name_res.is_error);
    path_name = result_path_name_checked(path_name_res);
    assert(str_is_equal(path_name_to_str(path_name, &arn), STR("/./blah/../../...")));
}

static void test_ram_fs_node_lookup(struct arena arn)
{
    struct ram_fs *rfs = ram_fs_new(test_helper_create_alloc(&arn));

    struct ram_fs_node *root_dir = arena_alloc_aligned(&arn, sizeof(*root_dir), alignof(*root_dir));
    struct ram_fs_node *blah_dir = arena_alloc_aligned(&arn, sizeof(*blah_dir), alignof(*blah_dir));
    struct ram_fs_node *foo_file = arena_alloc_aligned(&arn, sizeof(*foo_file), alignof(*foo_file));
    struct ram_fs_node *bar_file = arena_alloc_aligned(&arn, sizeof(*bar_file), alignof(*bar_file));

    root_dir->first = NULL;
    root_dir->next = NULL;
    root_dir->type = RAM_FS_TYPE_DIR;
    root_dir->name = STR("");
    root_dir->data = str_buf_new(NULL, 0, 0);
    root_dir->fs = rfs;

    blah_dir->first = NULL;
    blah_dir->next = NULL;
    blah_dir->type = RAM_FS_TYPE_DIR;
    blah_dir->name = STR("blah");
    blah_dir->data = str_buf_new(NULL, 0, 0);
    blah_dir->fs = rfs;

    foo_file->first = NULL;
    foo_file->next = NULL;
    foo_file->type = RAM_FS_TYPE_FILE;
    foo_file->name = STR("foo");
    foo_file->data = str_buf_new(NULL, 0, 0);
    foo_file->fs = rfs;

    bar_file->first = NULL;
    bar_file->next = NULL;
    bar_file->type = RAM_FS_TYPE_FILE;
    bar_file->name = STR("bar");
    bar_file->data = str_buf_new(NULL, 0, 0);
    bar_file->fs = rfs;

    root_dir->first = blah_dir;
    blah_dir->first = foo_file;
    foo_file->next = bar_file;

    rfs->root = root_dir;

    assert(ram_fs_node_lookup(rfs, result_path_name_checked(path_name_parse(STR("/"), &arn))) == root_dir);
    assert(ram_fs_node_lookup(rfs, result_path_name_checked(path_name_parse(STR("/blah"), &arn))) == blah_dir);
    assert(ram_fs_node_lookup(rfs, result_path_name_checked(path_name_parse(STR("/blah/foo"), &arn))) == foo_file);
    assert(ram_fs_node_lookup(rfs, result_path_name_checked(path_name_parse(STR("/blah/bar"), &arn))) == bar_file);
}

static void test_ram_fs_create_dir(struct arena arn)
{
    struct ram_fs *rfs = ram_fs_new(test_helper_create_alloc(&arn));

    struct result_ram_fs_node dir_res;

    // Create two directories /foo and /foo/bar

    dir_res = ram_fs_create_dir(rfs, STR("/foo"), false);
    assert(!dir_res.is_error);
    struct ram_fs_node *foo_dir = result_ram_fs_node_checked(dir_res);
    assert(foo_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(foo_dir->name, STR("foo")));

    dir_res = ram_fs_create_dir(rfs, STR("/foo/bar"), false);
    assert(!dir_res.is_error);
    struct ram_fs_node *bar_dir = result_ram_fs_node_checked(dir_res);
    assert(bar_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(bar_dir->name, STR("bar")));

    assert(rfs->root->first == foo_dir);
    assert(foo_dir->first == bar_dir);

    // Create another directory in /foo next to /foo/bar
    dir_res = ram_fs_create_dir(rfs, STR("/foo/baz"), false);
    assert(!dir_res.is_error);
    struct ram_fs_node *baz_dir = result_ram_fs_node_checked(dir_res);
    assert(baz_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(baz_dir->name, STR("baz")));

    assert(bar_dir->next == baz_dir);

    // Can't create the same directory again
    dir_res = ram_fs_create_dir(rfs, STR("/foo/bar"), false);
    assert(dir_res.is_error);
    assert(dir_res.code == EEXIST);

    // Can't create root directory
    dir_res = ram_fs_create_dir(rfs, STR("/"), false);
    assert(dir_res.is_error);
    assert(dir_res.code == EEXIST);

    // Can't create directory without parent directory
    dir_res = ram_fs_create_dir(rfs, STR("/this-doesn't-exist/bar/"), false);
    assert(dir_res.is_error);
    assert(dir_res.code == ENOENT);

    // Recursive directory creation works
    dir_res = ram_fs_create_dir(rfs, STR("/this-doesn't-exist/beep/boop/"), true);
    assert(!dir_res.is_error);
    struct ram_fs_node *boop_dir = result_ram_fs_node_checked(dir_res);
    assert(boop_dir->type == RAM_FS_TYPE_DIR);
    print_dbg(STR("boop_dir->name: %s\n"), boop_dir->name);
    assert(str_is_equal(boop_dir->name, STR("boop")));
}

static void test_ram_fs_create_file(struct arena arn)
{
    struct ram_fs *rfs = ram_fs_new(test_helper_create_alloc(&arn));

    struct result_ram_fs_node dir_res;
    struct result_ram_fs_node file_res;

    // Create a parent directory /foo for our files
    dir_res = ram_fs_create_dir(rfs, STR("/foo"), false);
    assert(!dir_res.is_error);
    struct ram_fs_node *foo_dir = result_ram_fs_node_checked(dir_res);
    assert(foo_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(foo_dir->name, STR("foo")));

    // Create a file /foo/bar.txt and verify its correctness
    file_res = ram_fs_create_file(rfs, STR("/foo/bar.txt"), false);
    assert(!file_res.is_error);
    struct ram_fs_node *bar_file = result_ram_fs_node_checked(file_res);
    assert(bar_file->type == RAM_FS_TYPE_FILE);
    assert(str_is_equal(bar_file->name, STR("bar.txt")));

    // Attempt to create the same file again
    file_res = ram_fs_create_file(rfs, STR("/foo/bar.txt"), false);
    assert(file_res.is_error);
    assert(file_res.code == EEXIST);

    // Create another file in the same directory and make sure the links are correct
    file_res = ram_fs_create_file(rfs, STR("/foo/baz.txt"), false);
    assert(!file_res.is_error);
    struct ram_fs_node *baz_file = result_ram_fs_node_checked(file_res);
    assert(baz_file->type == RAM_FS_TYPE_FILE);
    assert(str_is_equal(baz_file->name, STR("baz.txt")));

    assert(foo_dir->first == bar_file);
    assert(bar_file->next == baz_file);
    assert(baz_file->next == NULL);

    // Attempt to create a file in a non-existent parent directory
    file_res = ram_fs_create_file(rfs, STR("/nonexistent/dir/file.txt"), false);
    assert(file_res.is_error);
    assert(file_res.code == ENOENT);

    // Recursive file creation works
    file_res = ram_fs_create_file(rfs, STR("/nonexistent/dir/file.txt"), true);
    assert(!file_res.is_error);
    struct ram_fs_node *rec_file = result_ram_fs_node_checked(file_res);
    assert(rec_file->type == RAM_FS_TYPE_FILE);
    assert(str_is_equal(rec_file->name, STR("file.txt")));

    // Attempt to create a file inside another file
    file_res = ram_fs_create_file(rfs, STR("/foo/bar.txt/subfile"), false);
    assert(file_res.is_error);
    assert(file_res.code == ENOTDIR);

    // Recursive file creation doesn't work if the parent is a file
    file_res = ram_fs_create_file(rfs, STR("/foo/bar.txt/subfile"), true);
    assert(file_res.is_error);
    assert(file_res.code == ENOTDIR);

    // Attempt to create a file with a trailing '/'. We accept this as a valid call as calling the
    // function `ram_fs_create_file` clearly expresses the intent of creating a file.
    file_res = ram_fs_create_file(rfs, STR("/foo/trailing_slash/"), false);
    assert(!file_res.is_error);
}

static void test_ram_fs_open(struct arena arn)
{
    struct arena arn_cpy = arn; // Used to we can create fresh `struct ram_fs` structures twice.
    struct ram_fs *rfs = ram_fs_new(test_helper_create_alloc(&arn_cpy));

    // Manually setting up the file system structure to avoid dependency on file creation logic
    struct ram_fs_node *dir_node = arena_alloc_aligned(&arn_cpy, sizeof(*dir_node), alignof(*dir_node));
    struct ram_fs_node *file_node = arena_alloc_aligned(&arn_cpy, sizeof(*file_node), alignof(*file_node));

    dir_node->first = NULL;
    dir_node->next = NULL;
    dir_node->type = RAM_FS_TYPE_DIR;
    dir_node->name = STR("dir");
    dir_node->data = str_buf_new(NULL, 0, 0);
    dir_node->fs = rfs;

    file_node->first = NULL;
    file_node->next = NULL;
    file_node->type = RAM_FS_TYPE_FILE;
    file_node->name = STR("file");
    file_node->data = str_buf_new(NULL, 0, 0);
    file_node->fs = rfs;

    rfs->root->first = dir_node;
    dir_node->next = file_node;

    struct result_ram_fs_node res;

    // Root path returns root node
    res = ram_fs_open(rfs, STR("/"));
    assert(!res.is_error);
    assert(result_ram_fs_node_checked(res) == rfs->root);

    // Valid directory path returns directory node
    res = ram_fs_open(rfs, STR("/dir"));
    assert(!res.is_error);
    assert(result_ram_fs_node_checked(res) == dir_node);

    // Valid file path returns file node
    res = ram_fs_open(rfs, STR("/file"));
    assert(!res.is_error);
    assert(result_ram_fs_node_checked(res) == file_node);

    // Non-existent path returns ENOENT error
    res = ram_fs_open(rfs, STR("/invalid"));
    assert(res.is_error);
    assert(res.code == ENOENT);

    // Trailing slash on directory path is handled correctly
    res = ram_fs_open(rfs, STR("/dir/"));
    assert(!res.is_error);
    assert(result_ram_fs_node_checked(res) == dir_node);

    // Consecutive slashes result in ENOENT error
    res = ram_fs_open(rfs, STR("/dir//file"));
    assert(res.is_error);
    assert(res.code == ENOENT);

    // Can't use file as intermediate path
    res = ram_fs_open(rfs, STR("/file/dir"));
    assert(res.is_error);
    assert(res.code == ENOENT);

    // Path lookup in empty filesystem returns ENOENT error
    arn_cpy = arn;
    struct ram_fs *empty_rfs = ram_fs_new(test_helper_create_alloc(&arn_cpy));
    res = ram_fs_open(empty_rfs, STR("/dir"));
    assert(res.is_error);
    assert(res.code == ENOENT);

    // Case sensitivity results in ENOENT error for mismatched case
    res = ram_fs_open(rfs, STR("/DIR"));
    assert(res.is_error);
    assert(res.code == ENOENT);
}

static void test_ram_fs_read(struct arena arn)
{
    struct ram_fs *rfs = ram_fs_new(test_helper_create_alloc(&arn));

    struct result_sz res;
    struct str_buf sbuf;

    // Manually setting up the file system structure to avoid dependency on file creation logic
    struct ram_fs_node *file_node = arena_alloc_aligned(&arn, sizeof(*file_node), alignof(*file_node));
    file_node->first = NULL;
    file_node->next = NULL;
    file_node->type = RAM_FS_TYPE_FILE;
    file_node->name = STR("file");
    file_node->data = str_buf_from_arena(&arn, 13);
    file_node->fs = rfs;
    append_str(STR("Hello, world!"), &file_node->data);

    rfs->root->first = file_node;

    // Read the entire file
    sbuf = str_buf_from_arena(&arn, 13);
    res = ram_fs_read(file_node, &sbuf, 0);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 13);
    assert(str_is_equal(str_from_buf(sbuf), STR("Hello, world!")));

    // Read a part of the file
    sbuf = str_buf_from_arena(&arn, 5);
    res = ram_fs_read(file_node, &sbuf, 7);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 5);
    assert(str_is_equal(str_from_buf(sbuf), STR("world")));

    // Read past the end of the file
    sbuf = str_buf_from_arena(&arn, 5);
    res = ram_fs_read(file_node, &sbuf, 13);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 0);
    assert(str_is_equal(str_from_buf(sbuf), STR("")));

    // Read with offset past the end of the file
    sbuf = str_buf_from_arena(&arn, 5);
    res = ram_fs_read(file_node, &sbuf, 14);
    assert(res.is_error);
    assert(res.code == EINVAL);

    // Read with offset past the end of the file
    sbuf = str_buf_from_arena(&arn, 5);
    res = ram_fs_read(file_node, &sbuf, 15);
    assert(res.is_error);
    assert(res.code == EINVAL);

    // Reject reading from a directory
    struct ram_fs_node *dir_node = arena_alloc_aligned(&arn, sizeof(*dir_node), alignof(*dir_node));
    dir_node->first = NULL;
    dir_node->next = NULL;
    dir_node->type = RAM_FS_TYPE_DIR;
    dir_node->name = STR("dir");
    dir_node->data = str_buf_new(NULL, 0, 0);
    dir_node->fs = rfs;

    sbuf = str_buf_from_arena(&arn, 5);
    res = ram_fs_read(dir_node, &sbuf, 0);
    assert(res.is_error);
    assert(res.code == EINVAL);
}

static void test_ram_fs_write(struct arena arn)
{
    struct ram_fs *rfs = ram_fs_new(test_helper_create_alloc(&arn));

    struct result_sz res;

    // Manually setting up the file system structure to avoid dependency on file creation logic.
    struct ram_fs_node *file_node = arena_alloc_aligned(&arn, sizeof(*file_node), alignof(*file_node));
    file_node->first = NULL;
    file_node->next = NULL;
    file_node->type = RAM_FS_TYPE_FILE;
    file_node->name = STR("file");
    file_node->fs = rfs;
    file_node->data = str_buf_from_arena(&arn, 13);

    rfs->root->first = file_node;

    // Write to an empty file
    res = ram_fs_write(file_node, STR("Hello, world!"), 0);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 13);
    assert(str_is_equal(str_from_buf(file_node->data), STR("Hello, world!")));

    // Write to the beginning of the file
    res = ram_fs_write(file_node, STR("Adieu, "), 0);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 7);
    assert(str_is_equal(str_from_buf(file_node->data), STR("Adieu, world!")));

    // Write to the end of the file
    res = ram_fs_write(file_node, STR("!!!"), 13);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 3);
    assert(str_is_equal(str_from_buf(file_node->data), STR("Adieu, world!!!!")));

    // Write to the middle of the file
    res = ram_fs_write(file_node, STR("friend"), 7);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 6);
    assert(str_is_equal(str_from_buf(file_node->data), STR("Adieu, friend!!!")));

    // Write with offset past the end of the file
    res = ram_fs_write(file_node, STR("!!!"), 21);
    assert(res.is_error);
    assert(res.code == EINVAL);

    // Write with an offset close to the end of the file so part of the write exceeds the end of the file.
    res = ram_fs_write(file_node, STR("......"), 13);
    assert(!res.is_error);
    assert(result_sz_checked(res) == 6);
    assert(str_is_equal(str_from_buf(file_node->data), STR("Adieu, friend......")));
}

static void test_ram_fs_e2e(struct arena arn)
{
    struct ram_fs *rfs = ram_fs_new(test_helper_create_alloc(&arn));

    struct result_ram_fs_node res;
    struct result_sz res_sz;

    // Create a directory /foo and a file /foo/bar.txt
    res = ram_fs_create_dir(rfs, STR("/foo"), false);
    assert(!res.is_error);

    res = ram_fs_create_file(rfs, STR("/foo/bar.txt"), false);
    assert(!res.is_error);
    struct ram_fs_node *bar_file = result_ram_fs_node_checked(res);

    // Write to bar_file
    res_sz = ram_fs_write(bar_file, STR("Blah"), 0);
    assert(!res_sz.is_error);
    assert(result_sz_checked(res_sz) == 4);

    // Open the file again and write to it
    res = ram_fs_open(rfs, STR("/foo/bar.txt"));
    assert(!res.is_error);
    struct ram_fs_node *bar_file_opened = result_ram_fs_node_checked(res);

    res_sz = ram_fs_write(bar_file_opened, STR("Hello, world!"), 0);
    assert(!res_sz.is_error);
    assert(result_sz_checked(res_sz) == 13);

    // Open the file and read from it
    res = ram_fs_open(rfs, STR("/foo/bar.txt"));
    assert(!res.is_error);
    bar_file_opened = result_ram_fs_node_checked(res);

    struct str_buf sbuf = str_buf_from_arena(&arn, 13);
    res_sz = ram_fs_read(bar_file_opened, &sbuf, 0);
    assert(!res_sz.is_error);
    assert(result_sz_checked(res_sz) == 13);
    assert(str_is_equal(str_from_buf(sbuf), STR("Hello, world!")));
}

void ram_fs_run_tests(struct arena arn)
{
    test_path_name_parse(arn);
    test_path_name_to_str(arn);
    test_ram_fs_node_lookup(arn);
    test_ram_fs_create_dir(arn);
    test_ram_fs_create_file(arn);
    test_ram_fs_open(arn);
    test_ram_fs_read(arn);
    test_ram_fs_write(arn);
    test_ram_fs_e2e(arn);
    print_dbg(STR("RAM fs selftest passed\n"));
}
