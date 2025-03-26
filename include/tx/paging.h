#ifndef __TX_PAGING_H__
#define __TX_PAGING_H__

#include <tx/alloc.h>
#include <tx/arena.h>
#include <tx/assert.h>
#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/error.h>
#include <tx/pool.h>

struct pte {
    u64 bits;
} __packed;

static_assert(sizeof(struct pte) == 8);

#define NUM_PT_ENTRIES 512

struct pt {
    struct pte entries[NUM_PT_ENTRIES];
} __packed;

static_assert(sizeof(struct pt) == 0x1000);

struct page_table {
    struct pt *pml4;
};

typedef ptr vaddr_t;
typedef ptr paddr_t;

struct_result(paddr_t, paddr_t)

struct vas {
    struct page_table pt;
    // Allocator hands out virtual addresses from kernel memory that point to free pages. If the addresses
    // aren't from kernel memory, the underlying physical addresses won't be translated back to the correct
    // virtual addresses when trying to free it.
    struct alloc alloc;
};

struct vma {
    vaddr_t base;
    sz len;
};

// Specifies a linear mapping between a contiguous region of virtual and of physical memory.
struct addr_mapping {
    vaddr_t vbase;
    paddr_t pbase;
    sz len;
};

struct vaddr_range {
    vaddr_t base;
    sz len;
};

// Get the index that `vaddr` has in some page table page where `base` is the
// index of the first of the nine bits in `vaddr` that make up this index.
#define PT_IDX(vaddr, base) ((((sz)(BIT(9) - 1) << (base)) & (vaddr)) >> (base))

// NOTE: Use these flags with `pt_map`.
#define PT_FLAG_P BIT(0)
#define PT_FLAG_RW BIT(1)
#define PT_FLAG_US BIT(2)

#define PML4_BIT_BASE 39LU
#define PDPT_BIT_BASE 30LU
#define PD_BIT_BASE 21LU
#define PT_BIT_BASE 12LU

#define PTE_REGION_SIZE BIT(PT_BIT_BASE)
#define PDE_REGION_SIZE BIT(PD_BIT_BASE)

// To call this function, the beginning of the address range specified by `dyn_addrs` must
// already be mapped. This is because this function will use memory starting at `dyn_addrs.vbase`
// for page table pages. Not all of the `dyn_addrs.len` bytes will be used for page table pages,
// so not all of them need to be mapped before calling this function.
// Returns a contiguous region of virtual addresses that can be dynamically allocated by the kernel.
struct vaddr_range paging_init(struct addr_mapping code_addrs, struct addr_mapping dyn_addrs);

// Return kernel virtual address to currently used page table
struct page_table current_page_table(void);

// For `flags` see `PT_FLAG_*`.
struct result pt_map(struct page_table pt, vaddr_t vaddr, paddr_t paddr, int flags);
struct result_paddr_t pt_walk(struct page_table pt, vaddr_t vaddr);
struct result pt_unmap(struct page_table pt, vaddr_t vaddr);

// Walk the first `depth + 1` levels of the page table and print all existing entries.
// A depth of 0 will only print the PML4 and the maximum depth of 3 will print all
// mapped addresses. Prepare to use a very big buffer for depth 3.
// NOTE (IMPORTANT): This function dereferences physical addresses internally, because
// the addresses stored in page tables are all physical addresses. This means that
// this function will page fault if the physical addresses where the page table pages
// reside aren't identity mapped.
struct result pt_fmt(struct page_table page_table, struct str_buf *buf, i16 depth);

struct vas vas_new(struct page_table pt, struct alloc alloc);
struct vma vma_new(vaddr_t base, sz len);
// For `flags` see `pt_map`.
struct result vas_map(struct vas vas, struct vma vma, int flags);
struct result vas_unmap(struct vas vas, struct vma vma);
struct result vas_memcpy(struct vas vas, struct vma vma, struct bytes src);
struct result vas_memset(struct vas vas, struct vma vma, byte value);

#endif // __TX_PAGING_H__
