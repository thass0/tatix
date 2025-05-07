// Efficient buffer for constructing packets to be sent over the network.
//
// This data structure aims to reduce the number of copy operations required to construct a packet. When going down the
// layers of the protocol stack, protocol headers must often be _prepended_ to an existing buffer of data. This data
// structure allows constructing each protocol header exactly once without copying it again until the final packet is
// copied into the memory of a network driver.

#ifndef __TX_NET_SEND_BUF_H__
#define __TX_NET_SEND_BUF_H__

#include <tx/arena.h>
#include <tx/base.h>
#include <tx/byte.h>

#define SEND_BUF_NUM_PARTS 8

struct send_buf {
    struct arena arn;
    struct byte_buf parts[SEND_BUF_NUM_PARTS];
    sz n_used;
};

// Create a new send buffer that uses `arn` for its underlying memory.
struct send_buf send_buf_new(struct arena arn);

// Get a new byte buffer from the send buffer. The bytes written the the new buffer will be prepended to the content of
// all existing buffers in the send buffer when assembling the complete content of the send buffer.
struct byte_buf *send_buf_prepend(struct send_buf *sb, sz buf_size);

// Compute the total length of the send buffer's content. I.e., the length of the content appended the to the byte
// buffer when calling `send_buf_assemble`.
sz send_buf_total_length(struct send_buf sb);

// Append the complete content of the send buffer to `buf`.
struct result send_buf_assemble(struct send_buf sb, struct byte_buf *buf);

#endif // __TX_NET_SEND_BUF_H__
