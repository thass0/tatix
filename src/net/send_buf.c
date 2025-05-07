#include <tx/net/send_buf.h>

// NOTE: The complete content of the send buffer can be computed at any time by appending the content in the parts in
// reverse order. Thus, to prepend data to a send buffer, simply get the next part from the array of parts and append
// the data to this part.
//
// Each of the parts is allocated out of the arena allocator in the send buffer. The size of this arena allocator thus
// limits the capacity of the send buffer. Advantages of this scheme are that allocators from arenas are fast and that
// the memory behind all parts of a send buffer is in the same place.

struct send_buf send_buf_new(struct arena arn)
{
    struct send_buf sb;
    sb.arn = arn;
    byte_array_set(byte_array_new(&sb.parts, sizeof(sb.parts)), 0);
    sb.n_used = 0;
    return sb;
}

struct byte_buf *send_buf_prepend(struct send_buf *sb, sz buf_size)
{
    assert(sb);

    if (sb->n_used == SEND_BUF_NUM_PARTS)
        return NULL;

    struct byte_buf *buf = &sb->parts[sb->n_used];
    *buf = byte_buf_from_array(byte_array_from_arena(buf_size, &sb->arn));
    sb->n_used++;

    return buf;
}

sz send_buf_total_length(struct send_buf sb)
{
    sz len = 0;

    for (sz i = 0; i < sb.n_used; i++)
        len += sb.parts[i].len;

    return len;
}

struct result send_buf_assemble(struct send_buf sb, struct byte_buf *buf)
{
    assert(buf);

    sz len_before = buf->len;

    // Data is prepended to a send buffer by _appending_ it to the buffer `sb.parts[sb.n_used]` and then incrementing
    // the `n_used` member so the next prepend operation uses the next buffer. Hence, to assemble the content of the
    // send buffer in the right order, we need to append the content of all parts to `buf` starting with
    // `sb.parts[sb.n_used - 1]` and working our way backwards.

    for (sz i = sb.n_used; i > 0; i--) {
        sz n_appended = byte_buf_append(buf, byte_view_from_buf(sb.parts[i - 1]));
        if (n_appended != sb.parts[i - 1].len) {
            return result_error(ENOMEM);
        }
    }

    // To make sure we did this correctly. Callers rely on this invariant to be true.
    assert(buf->len - len_before == send_buf_total_length(sb));

    return result_ok();
}
