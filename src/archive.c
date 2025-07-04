#include <tx/archive.h>
#include <tx/ramfs.h>

u64 djb2_hash(struct byte_view bv)
{
    u64 hash = 5381;
    for (sz i = 0; i < bv.len; i++)
        hash = ((hash << 5) + hash) + bv.dat[i];
    return hash;
}

struct result archive_extract(struct byte_view archive, struct ram_fs *rfs)
{
    assert(rfs);

    if (archive.len < sizeof(struct ar_header))
        return result_error(EINVAL);

    // NOTE: We can just cast structs from teh archive bytes because
    // the archive uses little endian encoding, just like x86_64 does.
    // This is architecture specific though!
    struct ar_header *header = (struct ar_header *)archive.dat;

    if (!str_is_equal(str_new(header->magic, countof(header->magic)), MAGIC_STRING))
        return result_error(EINVAL);

    if (header->size > archive.len || header->size < 0 || header->index_length < 0)
        return result_error(EINVAL);

    sz index_offset = sizeof(struct ar_header);

    for (i64 i = 0; i < header->index_length; i++) {
        if (ADD_OVERFLOW((sz)archive.dat, index_offset))
            return result_error(EINVAL);
        if (index_offset >= archive.len)
            return result_error(EINVAL);
        struct ar_index_ent *index_ent = (struct ar_index_ent *)(archive.dat + index_offset);

        if (index_ent->offset < 0 || index_ent->size < 0 || index_ent->path_length < 0)
            return result_error(EINVAL);

        if (ADD_OVERFLOW(index_offset, index_ent->path_length) ||
            ADD_OVERFLOW(index_offset + index_ent->path_length, (sz)archive.dat) ||
            SUB_OVERFLOW(index_ent->size, index_ent->path_length))
            return result_error(EINVAL);

        if (index_ent->path_length > index_ent->size || index_offset + index_ent->size > archive.len)
            return result_error(EINVAL);

        struct str path = str_new((char *)(archive.dat + index_ent->offset), index_ent->path_length);
        struct byte_view data = byte_view_new(archive.dat + index_ent->offset + index_ent->path_length,
                                              index_ent->size - index_ent->path_length);
        struct byte_view path_and_data = byte_view_new(archive.dat + index_ent->offset, index_ent->size);

        if (djb2_hash(path_and_data) != index_ent->hash)
            return result_error(EINVAL);

        struct result_ram_fs_node node_res = ram_fs_create_file(rfs->root, path, true);
        if (node_res.is_error)
            return result_error(node_res.code);

        struct ram_fs_node *node = result_ram_fs_node_checked(node_res);
        ram_fs_write(node, data, 0);

        if (ADD_OVERFLOW(index_offset, sizeof(struct ar_index_ent)))
            return result_error(EINVAL);
        index_offset += sizeof(struct ar_index_ent);
    }

    return result_ok();
}
