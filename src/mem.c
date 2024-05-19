#include <mem.h>

#define mem_move_buf_size (1 << 15)
u8 mem_move_buf[mem_move_buf_size];

#define MEM_MOVE_IMPL(name, dst_type, src_type, n_type)         \
    size name(dst_type dst, src_type src, n_type n)             \
    {                                                           \
        if (dst == NULL | src == NULL | n <= 0)                 \
            return 0;                                           \
                                                                \
        n = n > mem_move_buf_size ? mem_move_buf_size : n;      \
                                                                \
        for (size i = 0; i < n; i++)                            \
            mem_move_buf[i] = ((u8*)src)[i];                    \
        for (size i = 0; i < n; i++)                            \
            ((u8*)dst)[i] = mem_move_buf[i];                    \
                                                                \
        return n;                                               \
    }

MEM_MOVE_IMPL(mem_move, void *, void *, size);
MEM_MOVE_IMPL(mem_move_volatile, volatile void *, volatile void *, size);
