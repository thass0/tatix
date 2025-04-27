// Kernel virtual address allocator (kvalloc)
//
// This allocator manages virtual memory for use by the kernel using the buddy system.
//
// The kvalloc implements a typical alloc/free interface. Kernel subsystems can get
// memory for their internal structures here. It's recommended that these subsystems
// make infrequent allocations and manage the memory they need internally.

// TODO(sync): kvalloc manages kernel memory, a resource shared among all processes.
// Accesses to kvalloc will requires synchronization.

#include <config.h>
#include <tx/arena.h>
#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/error.h>
#include <tx/kvalloc.h>
#include <tx/paging.h>

struct kvalloc {
    struct buddy *virt_alloc; // Manages virtual pages handed out by this allocator.
};

// We can't dynamically allocate memory for these structures because they are needed to
// initialize kvalloc, the dymamic allocator.

// An instance of a buddy allocator requires some memory for the heads of its free lists
// and for the bitmaps it uses. The amount of memory required depends on the size of the
// managed region because the bitmap increases in size with bigger regions.
#define VIRT_ALLOC_BACKING_MEM_SIZE 0x5000
static byte virt_alloc_backing_mem[VIRT_ALLOC_BACKING_MEM_SIZE];

static struct kvalloc global_kvalloc;
static bool global_kvalloc_is_initiallized = false;

struct result kvalloc_init(struct byte_array vaddrs)
{
    assert(!global_kvalloc_is_initiallized);

    struct arena arn = arena_new(byte_array_new(virt_alloc_backing_mem, VIRT_ALLOC_BACKING_MEM_SIZE));
    global_kvalloc.virt_alloc = buddy_init(vaddrs, &arn);

    global_kvalloc_is_initiallized = true;

    return result_ok();
}

struct option_byte_array kvalloc_alloc(sz n_bytes, sz align)
{
    assert(global_kvalloc_is_initiallized);

    // Pointers returned by the buddy allocator are naturally page-algined because the buddy
    // allocator works in page-sized blocks. We can handle bigger alignment later.
    assert(align <= PAGE_SIZE);

    sz real_size = ALIGN_UP(n_bytes, PAGE_SIZE);
    return buddy_alloc(global_kvalloc.virt_alloc, real_size);
}

void kvalloc_free(struct byte_array ba)
{
    assert(global_kvalloc_is_initiallized);

    if (!ba.dat)
        return;

    ba.len = ALIGN_UP(ba.len, PAGE_SIZE); // This is the real size we need to free.
    buddy_free(global_kvalloc.virt_alloc, ba);
}

void *kvalloc_alloc_wrapper(void *a __unused, sz size, sz align)
{
    struct option_byte_array ba = kvalloc_alloc(size, align);
    if (ba.is_none)
        return NULL;
    return byte_array_ptr(option_byte_array_checked(ba));
}

void kvalloc_free_wrapper(void *a __unused, void *ptr, sz size)
{
    kvalloc_free(byte_array_new(ptr, size));
}
