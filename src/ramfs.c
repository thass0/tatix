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
// Node creation                                                             //
///////////////////////////////////////////////////////////////////////////////

struct ram_fs_node *ram_fs_node_alloc(struct ram_fs *rfs, struct str name, enum ram_fs_node_type type)
{
    struct ram_fs_node *dir = pool_alloc(rfs->node_alloc);
    if (!dir)
        return NULL;
    dir->first = NULL;
    dir->next = NULL;
    dir->type = type;
    struct str_buf name_buf = str_buf_from_arena(&rfs->string_alloc, name.len);
    append_str(name, &name_buf);
    dir->name = str_from_buf(name_buf);
    dir->data = bytes_new(NULL, 0);
    return dir;
}

static struct result_ram_fs_node ram_fs_create_common(struct ram_fs *rfs, struct str nodepath,
                                                      enum ram_fs_node_type type, struct arena scratch)
{
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
        // Parent directory doesn't exist so this node can't be created.
        return result_ram_fs_node_error(ENOENT);
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

struct ram_fs ram_fs_new(struct buddy *alloc, struct arena *arn)
{
    struct ram_fs rfs;
    rfs.data_alloc = alloc;
    // TODO: Allow dynamically sized file systems.
    // TODO: Instead of passing a scratch arena we can have the RFS keep a
    // small arena that's used to parse paths into. In case we only ever use
    // the scratch arena to parse paths, this is a good choice beceause it's
    // easy to predict the maximum size for a path.
    rfs.node_alloc = pool_from_arena(RAM_FS_MAX_NODES_NUM, sizeof(struct ram_fs_node), arn);
    rfs.string_alloc = arena_new(bytes_from_arena(0x5000, arn));
    rfs.root = NULL; // TODO: Create a root as expected by the `create*` functions
    return rfs;
}

struct result_ram_fs_node ram_fs_create_dir(struct ram_fs *rfs, struct str dirpath, struct arena scratch)
{
    assert(rfs);
    return ram_fs_create_common(rfs, dirpath, RAM_FS_TYPE_DIR, scratch);
}

struct result_ram_fs_node ram_fs_create_file(struct ram_fs *rfs, struct str filepath, struct arena scratch)
{
    assert(rfs);
    return ram_fs_create_common(rfs, filepath, RAM_FS_TYPE_FILE, scratch);
}

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

static void test_ram_fs_create_dir(struct arena arn)
{
    struct buddy *rfs_data_alloc = buddy_init(bytes_from_arena(RAM_FS_MAX_BYTES_NUM, &arn), &arn);
    struct ram_fs rfs = ram_fs_new(rfs_data_alloc, &arn);

    struct ram_fs_node *root_dir = arena_alloc_aligned(&arn, sizeof(*root_dir), alignof(*root_dir));
    root_dir->first = NULL;
    root_dir->next = NULL;
    root_dir->type = RAM_FS_TYPE_DIR;
    root_dir->name = STR("");
    root_dir->data = bytes_new(NULL, 0);

    rfs.root = root_dir;

    struct result_ram_fs_node dir_res;

    // Create two directories /foo and /foo/bar

    dir_res = ram_fs_create_dir(&rfs, STR("/foo"), arn);
    assert(!dir_res.is_error);
    struct ram_fs_node *foo_dir = result_ram_fs_node_checked(dir_res);
    assert(foo_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(foo_dir->name, STR("foo")));

    dir_res = ram_fs_create_dir(&rfs, STR("/foo/bar"), arn);
    assert(!dir_res.is_error);
    struct ram_fs_node *bar_dir = result_ram_fs_node_checked(dir_res);
    assert(bar_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(bar_dir->name, STR("bar")));

    assert(root_dir->first == foo_dir);
    assert(foo_dir->first == bar_dir);

    // Create another directory in /foo next to /foo/bar
    dir_res = ram_fs_create_dir(&rfs, STR("/foo/baz"), arn);
    assert(!dir_res.is_error);
    struct ram_fs_node *baz_dir = result_ram_fs_node_checked(dir_res);
    assert(baz_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(baz_dir->name, STR("baz")));

    assert(bar_dir->next == baz_dir);

    // Can't create the same directory again
    dir_res = ram_fs_create_dir(&rfs, STR("/foo/bar"), arn);
    assert(dir_res.is_error);
    assert(dir_res.code == EEXIST);

    // Can't create root directory
    dir_res = ram_fs_create_dir(&rfs, STR("/"), arn);
    assert(dir_res.is_error);
    assert(dir_res.code == EEXIST);

    // Can't create directory without parent directory
    dir_res = ram_fs_create_dir(&rfs, STR("/this-doesn't-exist/bar/"), arn);
    assert(dir_res.is_error);
    assert(dir_res.code == ENOENT);
}

static void test_ram_fs_create_file(struct arena arn)
{
    struct buddy *rfs_data_alloc = buddy_init(bytes_from_arena(RAM_FS_MAX_BYTES_NUM, &arn), &arn);
    struct ram_fs rfs = ram_fs_new(rfs_data_alloc, &arn);

    struct ram_fs_node *root_dir = arena_alloc_aligned(&arn, sizeof(*root_dir), alignof(*root_dir));
    root_dir->first = NULL;
    root_dir->next = NULL;
    root_dir->type = RAM_FS_TYPE_DIR;
    root_dir->name = STR("");
    root_dir->data = bytes_new(NULL, 0);
    rfs.root = root_dir;

    struct result_ram_fs_node dir_res;
    struct result_ram_fs_node file_res;

    // Create a parent directory /foo for our files
    dir_res = ram_fs_create_dir(&rfs, STR("/foo"), arn);
    assert(!dir_res.is_error);
    struct ram_fs_node *foo_dir = result_ram_fs_node_checked(dir_res);
    assert(foo_dir->type == RAM_FS_TYPE_DIR);
    assert(str_is_equal(foo_dir->name, STR("foo")));

    // Create a file /foo/bar.txt and verify its correctness
    file_res = ram_fs_create_file(&rfs, STR("/foo/bar.txt"), arn);
    assert(!file_res.is_error);
    struct ram_fs_node *bar_file = result_ram_fs_node_checked(file_res);
    assert(bar_file->type == RAM_FS_TYPE_FILE);
    assert(str_is_equal(bar_file->name, STR("bar.txt")));

    // Attempt to create the same file again
    file_res = ram_fs_create_file(&rfs, STR("/foo/bar.txt"), arn);
    assert(file_res.is_error);
    assert(file_res.code == EEXIST);

    // Create another file in the same directory and make sure the links are correct
    file_res = ram_fs_create_file(&rfs, STR("/foo/baz.txt"), arn);
    assert(!file_res.is_error);
    struct ram_fs_node *baz_file = result_ram_fs_node_checked(file_res);
    assert(baz_file->type == RAM_FS_TYPE_FILE);
    assert(str_is_equal(baz_file->name, STR("baz.txt")));

    assert(foo_dir->first == bar_file);
    assert(bar_file->next == baz_file);
    assert(baz_file->next == NULL);

    // Attempt to create a file in a non-existent parent directory
    file_res = ram_fs_create_file(&rfs, STR("/nonexistent/dir/file.txt"), arn);
    assert(file_res.is_error);
    assert(file_res.code == ENOENT);

    // Attempt to create a file inside another file
    file_res = ram_fs_create_file(&rfs, STR("/foo/bar.txt/subfile"), arn);
    assert(file_res.is_error);
    assert(file_res.code == ENOTDIR);

    // Attempt to create a file with a trailing slash.
    // TODO: Add a flag to the parser to track if a trailing slash was found
    // file_res = ram_fs_create_file(&rfs, STR("/foo/trailing_slash/"), arn);
    // assert(file_res.is_error);
}

void ram_fs_run_tests(struct arena arn)
{
    test_path_name_parse(arn);
    test_ram_fs_node_lookup(arn);
    test_ram_fs_create_dir(arn);
    test_ram_fs_create_file(arn);
    print_str(STR("RAM FS TESTS PASSED!!!\n"));
}
