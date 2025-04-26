#ifndef __TX_PAGING_H__
#define __TX_PAGING_H__

#include <config.h>
#include <tx/base.h>
#include <tx/byte.h>
#include <tx/error.h>

#define PT_FLAG_P BIT(0)
#define PT_FLAG_RW BIT(1)
#define PT_FLAG_US BIT(2)
#define PT_FLAG_PWT BIT(3)
#define PT_FLAG_PCD BIT(4)

#define PML4_BIT_BASE 39LU
#define PDPT_BIT_BASE 30LU
#define PD_BIT_BASE 21LU
#define PT_BIT_BASE 12LU

#define PTE_REGION_SIZE BIT(PT_BIT_BASE)
#define PDE_REGION_SIZE BIT(PD_BIT_BASE)

// Get the index that `vaddr` has in some page table page where `base` is the
// index of the first of the nine bits in `vaddr` that make up this index.
#define PT_IDX(vaddr, base) ((((sz)(BIT(9) - 1) << (base)) & (vaddr)) >> (base))

struct pte {
    u64 bits;
} __packed;

static_assert(sizeof(struct pte) == 8);

#define NUM_PT_ENTRIES 512

struct pt {
    struct pte entries[NUM_PT_ENTRIES];
} __packed;

static_assert(sizeof(struct pt) == PAGE_SIZE);

struct page_table {
    struct pt *pml4;
};

typedef ptr vaddr_t;
typedef ptr paddr_t;

struct_result(vaddr_t, vaddr_t);
struct_result(paddr_t, paddr_t);

// Paging allows multiple different virtual addresses to point to the same physical address. This means that
// by default there is no unique mapping from a given physical address to a corresponding virtual address. We
// address this by introducing two kinds of mappings: canonical mappings and alias mappings.
//
// Canonical mappings must be unique. There can only be one canonical mapping for any physical address. But if
// there is a canonical mapping for a physical address, an arbitrary number of alias mappings for the same physical
// address may also exist. In such a case, a physical address is translated to its virtual address by looking it
// up in the canonical mapping.
//
// If there is no canonical mapping, there can only by one alias mapping for any physical address to ensure
// uniqueness. In this case, a physical address is translated to its virtual address by looking it up in this
// unique alias mapping.
enum addr_mapping_type {
    ADDR_MAPPING_TYPE_CANONICAL,
    ADDR_MAPPING_TYPE_ALIAS,
};

// These are the default memory types with respect to cache control. In a page table entry, the PWT and
// PCD bits control which memory type is used to access the memory pointed to by the page table entry.
// The PAT can also contribute to the memory type although, by default, it doesn't have any effect.
//
// See Section 49 and Tables 12-11 and 12-12 from the IA-32 Software Developers Manual Volume 3.
//
// NOTE: Modern x86_64 processors also have Memory Type Range Registers (MTRRs) for associating memory types with
// ranges of physical memory. The operating system is free, though, to modify the memory map only with page-level
// cacheability attributes (see Section 12.11).
enum addr_mapping_memory_type {
    ADDR_MAPPING_MEMORY_DEFAULT = 0, // By default, the PWT and PCD bits are the same as for WB.
    ADDR_MAPPING_MEMORY_WRITE_BACK = 0, // "WB"
    ADDR_MAPPING_MEMORY_WRITE_THROUGH, // "WT"
    ADDR_MAPPING_MEMORY_UNCACHEABLE, // "UC-"
    ADDR_MAPPING_MEMORY_STRONG_UNCACHEABLE, // "UC"
};

// Specifies a linear mapping between a contiguous region of virtual and of physical memory.
struct addr_mapping {
    enum addr_mapping_type type;
    enum addr_mapping_memory_type mem_type;
    u16 perms; // This just stores the `P_FLAG_*` values corresponding to the requested permissions.
    vaddr_t vbase;
    paddr_t pbase;
    sz len;
};

// To call this function, the beginning of the address range specified by `dyn_addrs` must
// already be mapped. This is because this function will use memory starting at `dyn_addrs.vbase`
// for page table pages. Not all of the `dyn_addrs.len` bytes will be used for page table pages,
// so not all of them need to be mapped before calling this function.
// Returns a contiguous region of virtual addresses that can be dynamically allocated by the kernel.
struct byte_array paging_init(struct addr_mapping code_addrs, struct addr_mapping dyn_addrs);

struct result paging_map_region(struct addr_mapping addrs);
struct result paging_unmap_region(struct addr_mapping addrs);

// Translate between physical and virtual addresses. The translations are based on the address mappings
// that were created by calls to `paging_init` and/or `paging_map_region` and `paging_unmap_region`.
struct result_paddr_t virt_to_phys(vaddr_t vaddr);
struct result_vaddr_t phys_to_virt(paddr_t paddr);

#endif // __TX_PAGING_H__
