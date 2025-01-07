#include "tx/stringdef.h"
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
    sz n_components;
    struct str *components;
    // True if the path started with '/'. If the path is only '/', without any other components,
    // this is true and `n_components` is 0.
    bool is_absolute;
};

struct_result(path_name, struct path_name)

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
// Public functions                                                          //
///////////////////////////////////////////////////////////////////////////////

struct ram_fs ram_fs_new(struct buddy *alloc, struct arena *arn)
{
    struct ram_fs rfs;
    rfs.data_alloc = alloc;
    // TODO: Allow dynamically sized file systems.
    rfs.node_alloc = pool_from_arena(RAM_FS_MAX_NODES_NUM, sizeof(struct ram_fs_node), arn);
    rfs.root = NULL;
    return rfs;
}

struct result_ram_fs_node ram_fs_create_dir(struct ram_fs *rfs, struct str dirname);

struct result_ram_fs_node ram_fs_create(struct ram_fs *rfs, struct str filename);

struct result_ram_fs_node ram_fs_open(struct ram_fs *rfs, struct str filename);

struct result_sz ram_fs_read(struct ram_fs_node *rfs_node, struct str_buf sbuf, sz offset);

struct result_sz ram_fs_write(struct ram_fs_node *rfs_node, struct str str, sz offset);

///////////////////////////////////////////////////////////////////////////////
// Tests                                                                     //
///////////////////////////////////////////////////////////////////////////////

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

static void test_ram_fs_node_lookup(struct arena arn)
{
    struct buddy *rfs_data_alloc = buddy_init(bytes_from_arena(RAM_FS_MAX_BYTES_NUM, &arn), &arn);
    struct ram_fs rfs = ram_fs_new(rfs_data_alloc, &arn);

    struct ram_fs_node *root_dir = arena_alloc_aligned(&arn, sizeof(*root_dir), alignof(*root_dir));
    struct ram_fs_node *blah_dir = arena_alloc_aligned(&arn, sizeof(*blah_dir), alignof(*blah_dir));
    struct ram_fs_node *foo_file = arena_alloc_aligned(&arn, sizeof(*foo_file), alignof(*foo_file));
    struct ram_fs_node *bar_file = arena_alloc_aligned(&arn, sizeof(*bar_file), alignof(*bar_file));

    root_dir->first = NULL;
    root_dir->next = NULL;
    root_dir->type = RAM_FS_TYPE_DIR;
    root_dir->name = STR("");
    root_dir->data = bytes_new(NULL, 0);

    blah_dir->first = NULL;
    blah_dir->next = NULL;
    blah_dir->type = RAM_FS_TYPE_DIR;
    blah_dir->name = STR("blah");
    blah_dir->data = bytes_new(NULL, 0);

    foo_file->first = NULL;
    foo_file->next = NULL;
    foo_file->type = RAM_FS_TYPE_FILE;
    foo_file->name = STR("foo");
    foo_file->data = bytes_new(NULL, 0);

    bar_file->first = NULL;
    bar_file->next = NULL;
    bar_file->type = RAM_FS_TYPE_FILE;
    bar_file->name = STR("bar");
    bar_file->data = bytes_new(NULL, 0);

    root_dir->first = blah_dir;
    blah_dir->first = foo_file;
    foo_file->next = bar_file;

    rfs.root = root_dir;

    assert(ram_fs_node_lookup(&rfs, result_path_name_checked(path_name_parse(STR("/"), &arn))) == root_dir);
    assert(ram_fs_node_lookup(&rfs, result_path_name_checked(path_name_parse(STR("/blah"), &arn))) == blah_dir);
    assert(ram_fs_node_lookup(&rfs, result_path_name_checked(path_name_parse(STR("/blah/foo"), &arn))) == foo_file);
    assert(ram_fs_node_lookup(&rfs, result_path_name_checked(path_name_parse(STR("/blah/bar"), &arn))) == bar_file);
}

void ram_fs_run_tests(struct arena arn)
{
    test_ram_fs_node_lookup(arn);
    test_path_name_parse(arn);
    print_str(STR("RAM FS TESTS PASSED!!!\n"));
}
