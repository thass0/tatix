#include <config.h>
#include <tx/buddy.h>

// The algorithms in this implementation of the buddy system are from
// Donald Knuths' The Art of Computer Programming, Volume 1. See 2.5.

static inline sz max_len(sz n)
{
    // The maximum length that we can use is the biggest power of two that's not greater than `n`
    for (sz shift = 1; shift < sizeof(n) * BYTE_WIDTH; shift <<= 1)
        n |= n >> shift;
    return (n + 1) >> 1;
}

static inline sz order_of(sz len)
{
    assert(len > 0 && (len & (len - 1)) == 0);
    int ord = 0;
    while (len >>= 1)
        ord++;
    return ord;
}

static inline sz length_of_order(sz ord)
{
    assert(ord < SZ_WIDTH);
    return ((sz)1LL) << ord;
}

struct buddy *buddy_init(struct bytes area, struct arena *arn)
{
    assert(area.len > 0);
    assert(area.dat);
    assert(arn);

    sz padding = -(uptr)area.dat & (PAGE_SIZE - 1);
    sz avail = area.len - padding;
    assert(avail > 0);
    sz n_pages = max_len(avail) / PAGE_SIZE;
    byte *base = area.dat + padding;
    sz bitmap_len = ((n_pages + BYTE_WIDTH) - 1) / BYTE_WIDTH;

    struct buddy *buddy = arena_alloc(arn, sizeof(*buddy));
    buddy->bitmap = bytes_from_arena(bitmap_len, arn);

    for (sz i = 0; i < N_FREE_LISTS; i++)
        dlist_init_empty(&buddy->avail[i].link);

    buddy->max_ord = order_of(n_pages);
    buddy->base = base;

    struct block *block = (struct block *)base;
    block->ord = buddy->max_ord;
    assert(buddy->max_ord < N_FREE_LISTS);
    dlist_insert(&buddy->avail[buddy->max_ord].link, &block->link);

    print_dbg(STR("Initialized buddy: base=0x%lx max_ord=0x%ld\n"), buddy->base, buddy->max_ord);
    return buddy;
}

static void set_bitmap(struct buddy *buddy, void *addr, sz ord, byte value)
{
    assert(buddy);
    assert(ord >= 0);

    sz bit_idx = ((byte *)addr - buddy->base) / PAGE_SIZE;
    sz bit_end = bit_idx + (1 << ord);
    assert(buddy->bitmap.len <= SZ_MAX / BYTE_WIDTH);
    assert(bit_end <= buddy->bitmap.len * BYTE_WIDTH);
    for (; bit_idx < bit_end; bit_idx++) {
        buddy->bitmap.dat[bit_idx / BYTE_WIDTH] &= ~(1 << (bit_idx % BYTE_WIDTH));
        buddy->bitmap.dat[bit_idx / BYTE_WIDTH] |= value << (bit_idx % BYTE_WIDTH);
    }
}

static inline void set_avail(struct buddy *buddy, void *addr, sz ord)
{
    set_bitmap(buddy, addr, ord, 1);
}

static inline void set_not_avail(struct buddy *buddy, void *addr, sz ord)
{
    set_bitmap(buddy, addr, ord, 0);
}

static inline struct block *split_block(struct block *block, sz ord)
{
    // Note that the memory is divided into units of page size.
    sz len = length_of_order(ord);
    assert(len >= 0);
    assert(len <= SZ_MAX / PAGE_SIZE);
    sz offset = len * PAGE_SIZE;
    assert((ptr)block <= SZ_MAX - offset);
    return (struct block *)((ptr)block + offset);
}

void *buddy_alloc(struct buddy *buddy, sz req_ord)
{
    assert(req_ord >= 0);
    assert(buddy);

    sz ord = req_ord;
    while (ord <= buddy->max_ord) {
        if (!dlist_is_empty(&buddy->avail[ord].link))
            break;
        ord++;
    }

    if (ord > buddy->max_ord) {
        print_dbg(STR("No block found, all blocks too small: ord=%ld\n"), req_ord);
        return NULL;
    }

    struct block *ret = __container_of(buddy->avail[ord].link.next, struct block, link);
    set_not_avail(buddy, ret, ord);
    dlist_remove(buddy->avail[ord].link.next);

    if (ord == req_ord) {
        print_dbg(STR("Found perfect fit: ret=0x%lx ord=0x%ld\n"), ret, ord);
        return ret;
    }

    // Split blocks until the order of the block that we return matches the order requested
    while (ord > req_ord) {
        ord -= 1;
        struct block *rem = split_block(ret, ord);
        set_avail(buddy, rem, ord);
        rem->ord = ord;
        dlist_insert(&buddy->avail[ord].link, &rem->link);
        print_dbg(STR("Split blocks: ret=0x%lx rem=0x%lx ord=%ld\n"), ret, rem, ord);
    }

    print_dbg(STR("Found block after splitting: ret=0x%lx ord=0x%ld\n"), ret, ord);
    return ret;
}

static inline struct block *get_buddy(struct buddy *buddy, struct block *block, sz ord)
{
    ptr base_offset = (ptr)block - (ptr)buddy->base;
    assert(length_of_order(ord + 1) <= SZ_MAX / PAGE_SIZE);
    sz mod = length_of_order(ord + 1) * PAGE_SIZE;
    assert(length_of_order(ord) <= SZ_MAX / PAGE_SIZE);
    sz halfway_offset = length_of_order(ord) * PAGE_SIZE;
    if (base_offset % mod == 0) {
        return (struct block *)((ptr)block + halfway_offset);
    } else if (base_offset % mod == halfway_offset) {
        return (struct block *)((ptr)block - halfway_offset);
    } else {
        crash("Invalid block pointer\n");
    }
}

static inline bool is_avail(struct buddy *buddy, void *addr)
{
    assert(buddy);
    sz bit_idx = ((ptr)addr - (ptr)buddy->base) / PAGE_SIZE;
    sz byte_idx = bit_idx / BYTE_WIDTH;
    assert(0 <= bit_idx);
    assert(0 <= byte_idx && byte_idx < buddy->bitmap.len);
    return (buddy->bitmap.dat[byte_idx] & (1 << (bit_idx % BYTE_WIDTH))) != 0;
}

void buddy_free(struct buddy *buddy, void *ptr, sz ord)
{
    assert(buddy);
    assert(0 <= ord && ord <= buddy->max_ord);

    struct block *block = ptr;
    struct block *buddy_block = get_buddy(buddy, block, ord);

    print_dbg(STR("Freeing block: block=0x%lx ord=%ld\n"), block, ord);

    while (ord < buddy->max_ord && is_avail(buddy, buddy_block)) {
        print_dbg(STR("Coalescing blocks: block=0x%lx buddy_block=0x%lx ord=%ld\n"), block, buddy_block, ord);
        dlist_remove(&buddy_block->link);
        ord++;
        if (buddy_block < block)
            block = buddy_block;
        buddy_block = get_buddy(buddy, block, ord);
    }

    set_avail(buddy, block, ord);
    dlist_insert(&buddy->avail[ord].link, &block->link);
}
