#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/fmt.h>
#include <tx/paging.h>
#include <tx/pool.h>

// NOTE(VERY IMPORTANT): By default, all pointers use virtual addresses in the virtual
// memory areas used by the kernel (high memory). These can be assumed to be safe to
// dereference. Care should be taken when dealing with physical addresses and with
// virtual addresses outside of kernel memory.

// TODO(sync): Protect global variables.

// Allocator used to allocate page table pages. It returns virtual memory addresses.
// Initialized by `paging_init`.
static struct pool global_pt_page_alloc;
// Root of global page table. Initialized by `paging_init`.
static struct page_table global_page_table;

///////////////////////////////////////////////////////////////////////////////
// Virt-phys mappings                                                        //
///////////////////////////////////////////////////////////////////////////////

#define NUM_ADDR_MAPPINGS 32
static struct addr_mapping global_addr_mappings[NUM_ADDR_MAPPINGS];
static bool global_addr_mappings_used[NUM_ADDR_MAPPINGS];

static inline bool intervals_overlap(sz a1, sz b1, sz a2, sz b2)
{
    return a1 < b2 && a2 < b1;
}

static struct result add_addr_mapping(struct addr_mapping new_mapping)
{
    sz n_canonical = 0;
    sz n_alias = 0;

    // Make sure there are no conflicts.
    for (i32 i = 0; i < NUM_ADDR_MAPPINGS; i++) {
        if (global_addr_mappings_used[i]) {
            struct addr_mapping *mapping = &global_addr_mappings[i];
            // Two different virtual addresses are allowed to point to the same physical address, but there cannot be
            // two different virt-to-phys mappings for the same virtual address. This means that we don't accept
            // overlapping virtual address regions for any kind of address mapping.
            if (intervals_overlap(new_mapping.vbase, new_mapping.vbase + new_mapping.len, mapping->vbase,
                                  mapping->vbase + mapping->len))
                return result_error(EINVAL);

            // For physical addresses, we just count how many overlapping canonical and alias mappings
            // there are. See below for how these are used.
            if (intervals_overlap(new_mapping.pbase, new_mapping.pbase + new_mapping.len, mapping->pbase,
                                  mapping->pbase + mapping->len)) {
                switch (mapping->type) {
                case ADDR_MAPPING_TYPE_CANONICAL:
                    n_canonical++;
                    break;
                case ADDR_MAPPING_TYPE_ALIAS:
                    n_alias++;
                    break;
                default:
                    crash("Invalid mapping type");
                }
            }
        }
    }

    // The key invariant to make phys-to-virt translations work is this: Either there is one canonical
    // mapping and an arbitrary number of alias mappings, or there are no canonical mappings and one alias mapping
    // (or no mappings at all).
    if (!(n_canonical == 1 || (n_canonical == 0 && n_alias == 1) || (n_canonical == 0 && n_alias == 0)))
        return result_error(EINVAL);

    // We now know the new mapping doesn't introduce any conflics. So let's create it!
    for (i32 i = 0; i < NUM_ADDR_MAPPINGS; i++) {
        if (!global_addr_mappings_used[i]) {
            global_addr_mappings[i] = new_mapping;
            global_addr_mappings_used[i] = true;
            return result_ok();
        }
    }

    return result_error(ENOMEM);
}

static struct result remove_addr_mapping(struct addr_mapping mapping)
{
    for (i32 i = 0; i < NUM_ADDR_MAPPINGS; i++) {
        if (global_addr_mappings_used[i] && global_addr_mappings[i].vbase == mapping.vbase &&
            global_addr_mappings[i].pbase == mapping.pbase && global_addr_mappings[i].len == mapping.len) {
            global_addr_mappings_used[i] = false;
            return result_ok();
        }
    }

    return result_error(EINVAL);
}

// NOTE: Multiple virtual addresses can point to the same physical address. This function returns the
// virtual address (in high memory) that's used by the kernel to access the physical address. There may
// be mappings that use a different virtual address to access the same physical page.
struct result_vaddr_t phys_to_virt(paddr_t paddr)
{
    if (!paddr)
        return result_vaddr_t_ok(0); // To not have to check for NULL before calling this function.

    struct addr_mapping *canonical = NULL;
    struct addr_mapping *alias = NULL;
    sz n_canonical = 0;
    sz n_alias = 0;

    for (i32 i = 0; i < NUM_ADDR_MAPPINGS; i++) {
        if (global_addr_mappings_used[i]) {
            struct addr_mapping *mapping = &global_addr_mappings[i];
            if (IN_RANGE(paddr, mapping->pbase, mapping->len)) {
                if (mapping->type == ADDR_MAPPING_TYPE_CANONICAL) {
                    canonical = mapping;
                    n_canonical++;
                } else {
                    alias = mapping;
                    n_alias++;
                }
            }
        }
    }

    // Just check this invariant for safety.
    assert(n_canonical == 1 || (n_canonical == 0 && n_alias == 1) || (n_canonical == 0 && n_alias == 0));

    if (canonical)
        return result_vaddr_t_ok(canonical->vbase + (paddr - canonical->pbase)); // Use the unique canonical mapping.
    else if (alias)
        return result_vaddr_t_ok(
            alias->vbase +
            (paddr - alias->pbase)); // When there is no canonical mapping, the alias must be unique so we can use it.

    return result_vaddr_t_error(EINVAL);
}

struct result_paddr_t virt_to_phys(vaddr_t vaddr)
{
    if (!vaddr)
        return result_paddr_t_ok(0); // To not have to check for NULL before calling this function.

    struct addr_mapping *candidate = NULL;
    sz n_candidates = 0;

    for (i32 i = 0; i < NUM_ADDR_MAPPINGS; i++) {
        if (global_addr_mappings_used[i]) {
            struct addr_mapping *mapping = &global_addr_mappings[i];
            if (IN_RANGE(vaddr, mapping->vbase, mapping->len)) {
                candidate = mapping;
                n_candidates++;
            }
        }
    }

    // Just check this invariant for safety.
    assert(n_candidates <= 1);

    if (candidate)
        return result_paddr_t_ok(candidate->pbase + (vaddr - candidate->vbase));

    return result_paddr_t_error(EINVAL);
}

///////////////////////////////////////////////////////////////////////////////
// Creating mappings and walking page tables                                 //
///////////////////////////////////////////////////////////////////////////////

static inline u16 mem_type_flags(enum addr_mapping_memory_type mt)
{
    // See Tables 12-11 and 12-12 of the IA-32 Software Developers Manual Volume 3.
    switch (mt) {
    case ADDR_MAPPING_MEMORY_WRITE_BACK:
        return 0;
    case ADDR_MAPPING_MEMORY_WRITE_THROUGH:
        return PT_FLAG_PWT;
    case ADDR_MAPPING_MEMORY_UNCACHEABLE:
        return PT_FLAG_PCD;
    case ADDR_MAPPING_MEMORY_STRONG_UNCACHEABLE:
        return PT_FLAG_PCD | PT_FLAG_PWT;
    }
    crash("Invalid memory type\n");
}

static inline paddr_t paddr_from_pte(struct pte pte)
{
    return pte.bits & ~(BIT(11) - 1);
}

static inline struct pte pte_from_paddr(paddr_t ptr, u16 perms, enum addr_mapping_memory_type mem_type)
{
    return (struct pte){ (ptr & ~(BIT(11) - 1)) | perms | mem_type_flags(mem_type) | PT_FLAG_P };
}

static void pt_insert(struct pt *pt, sz idx, struct pte pte)
{
    assert(pt);
    assert(0 <= idx && idx < countof(pt->entries));

    pt->entries[idx] = pte;
}

// Always returns a virtual address
static struct pt *pt_get(struct pt *pt, sz idx)
{
    assert(pt);
    assert(0 <= idx && idx < countof(pt->entries));

    if (pt->entries[idx].bits & PT_FLAG_P) {
        paddr_t paddr = paddr_from_pte(pt->entries[idx]);
        return (struct pt *)result_vaddr_t_checked(phys_to_virt(paddr));
    }
    return NULL;
}

static struct pt *pt_get_or_alloc(struct pt *pt, sz idx, u16 perms)
{
    struct pt *ret;

    ret = pt_get(pt, idx);

    if (!ret) {
        ret = pool_alloc(&global_pt_page_alloc);
        if (!ret)
            return NULL;
        paddr_t paddr_ret = result_paddr_t_checked(virt_to_phys((vaddr_t)ret));
        print_dbg(PDBG, STR("Allocated page table page: vaddr=0x%lx paddr=0x%lx\n"), ret, paddr_ret);
        pt_insert(pt, idx, pte_from_paddr(paddr_ret, perms, ADDR_MAPPING_MEMORY_DEFAULT));
    } else {
        struct pte pte = pt->entries[idx]; // Safe because `pt_get` succeeded.
        // Update the existing PTE if the new flags are _more_ permissive than before.
        if ((perms & PT_FLAG_US) && !(pte.bits & PT_FLAG_US))
            pte.bits |= PT_FLAG_US;
        if ((perms & PT_FLAG_RW) && !(pte.bits & PT_FLAG_RW))
            pte.bits |= PT_FLAG_RW;
        pt_insert(pt, idx, pte);
    }

    return ret;
}

static struct result pt_map(struct page_table page_table, vaddr_t vaddr, paddr_t paddr, u16 perms,
                            enum addr_mapping_memory_type mem_type)
{
    struct pt *pdpt = pt_get_or_alloc(page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE), perms);
    if (!pdpt)
        return result_error(ENOMEM);
    struct pt *pd = pt_get_or_alloc(pdpt, PT_IDX(vaddr, PDPT_BIT_BASE), perms);
    if (!pd)
        return result_error(ENOMEM);
    struct pt *pt = pt_get_or_alloc(pd, PT_IDX(vaddr, PD_BIT_BASE), perms);
    if (!pd)
        return result_error(ENOMEM);
    sz pt_idx = PT_IDX(vaddr, PT_BIT_BASE);
    assert(0 <= pt_idx && pt_idx < countof(pt->entries));
    // NOTE: The reason we handle the memory type separate from the flags is that the flags are applied to
    // all page table pages along the way while the memory type is only applied to the final mapping. So the
    // flags are more like permissions.
    pt->entries[pt_idx] = pte_from_paddr(paddr, perms, mem_type);
    return result_ok();
}

bool pt_is_empty(struct pt *pt)
{
    assert(pt);
    for (sz i = 0; i < countof(pt->entries); i++) {
        if (pt->entries[i].bits & PT_FLAG_P)
            return false;
    }
    return true;
}

static struct result pt_unmap(struct page_table page_table, vaddr_t vaddr)
{
    if (!page_table.pml4)
        return result_error(EINVAL);
    struct pt *pdpt = pt_get(page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE));
    if (!pdpt)
        return result_error(EINVAL);
    struct pt *pd = pt_get(pdpt, PT_IDX(vaddr, PDPT_BIT_BASE));
    if (!pd)
        return result_error(EINVAL);
    struct pt *pt = pt_get(pd, PT_IDX(vaddr, PD_BIT_BASE));
    if (!pt)
        return result_error(EINVAL);
    sz pt_idx = PT_IDX(vaddr, PT_BIT_BASE);
    assert(0 <= pt_idx && pt_idx < countof(pt->entries));
    if (!(pt->entries[pt_idx].bits & PT_FLAG_P))
        return result_error(EINVAL);
    pt->entries[pt_idx].bits &= ~PT_FLAG_P;
    print_dbg(PDBG, STR("Removed entry: pt_idx=%ld\n"), pt_idx);

    if (pt_is_empty(pt)) {
        print_dbg(PDBG, STR("Freeing page table: pt=0x%lx\n"), pt);
        pd->entries[PT_IDX(vaddr, PD_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(&global_pt_page_alloc, pt);
    }
    if (pt_is_empty(pd)) {
        print_dbg(PDBG, STR("Freeing page directory: pd=0x%lx\n"), pd);
        pdpt->entries[PT_IDX(vaddr, PDPT_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(&global_pt_page_alloc, pd);
    }
    if (pt_is_empty(pdpt)) {
        print_dbg(PDBG, STR("Freeing page directory pointer table: pdpt=0x%lx\n"), pdpt);
        page_table.pml4->entries[PT_IDX(vaddr, PML4_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(&global_pt_page_alloc, pdpt);
    }

    return result_ok();
}

static void _pt_fmt_indent(struct str_buf *buf, i16 level)
{
    assert(buf);
    // OOM errors in `append_str` are ignored here because they will be caught
    // the next time `buf` is used for formatting.
    for (i16 i = 0; i < level; i++)
        str_buf_append(buf, STR("    "));
}

__unused static struct result _pt_fmt(struct pt *pt, struct str_buf *buf, i16 level, i16 depth, vaddr_t base_vaddr)
{
    assert(pt);
    assert(buf);
    assert(level >= 0 && level <= depth);
    assert(depth <= 3);

    struct result res = result_ok();
    struct pte entry = { 0 };
    vaddr_t vaddr = 0;

    for (sz idx = 0; idx < countof(pt->entries); idx++) {
        entry = pt->entries[idx];
        if (entry.bits & PT_FLAG_P) {
            vaddr = base_vaddr + (idx << (PML4_BIT_BASE - (level * 9)));
            _pt_fmt_indent(buf, level);
            res = fmt(buf, STR("%ld : %c%c vaddr=0x%lx paddr=0x%lx\n"), idx, (entry.bits & PT_FLAG_US) ? 'u' : 's',
                      (entry.bits & PT_FLAG_RW) ? 'w' : 'r', vaddr, paddr_from_pte(entry));
            if (res.is_error)
                return res;
            if (level < depth) {
                res = _pt_fmt((struct pt *)paddr_from_pte(entry), buf, level + 1, depth, vaddr);
                if (res.is_error)
                    return res;
            }
        }
    }

    return res;
}

// Walk the first `depth + 1` levels of the page table and print all existing entries.
// A depth of 0 will only print the PML4 and the maximum depth of 3 will print all
// mapped addresses. Prepare to use a very big buffer for depth 3.
// NOTE (IMPORTANT): This function dereferences physical addresses internally, because
// the addresses stored in page tables are all physical addresses. This means that
// this function will page fault if the physical addresses where the page table pages
// reside aren't identity mapped.
__unused static struct result pt_fmt(struct page_table page_table, struct str_buf *buf, i16 depth)
{
    assert(buf);
    assert(depth <= 3);
    return _pt_fmt(page_table.pml4, buf, 0, depth, 0);
}

///////////////////////////////////////////////////////////////////////////////
// Outward-facing interface                                                  //
///////////////////////////////////////////////////////////////////////////////

static bool cpu_has_pat(void)
{
    u32 eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return (edx & BIT(16)) != 0;
}

struct byte_array paging_init(struct addr_mapping code_addrs, struct addr_mapping dyn_addrs)
{
    assert(PAGE_SIZE == 0x1000);

    sz n_pages = ALIGN_UP(code_addrs.len + dyn_addrs.len, PAGE_SIZE) / PAGE_SIZE;
    sz n_pts = ALIGN_UP(n_pages, NUM_PT_ENTRIES) / NUM_PT_ENTRIES;
    sz n_pds = ALIGN_UP(n_pts, NUM_PT_ENTRIES) / NUM_PT_ENTRIES;
    sz n_pdpts = ALIGN_UP(n_pds, NUM_PT_ENTRIES) / NUM_PT_ENTRIES;
    sz n_pml4s = ALIGN_UP(n_pdpts, NUM_PT_ENTRIES) / NUM_PT_ENTRIES;

    // We reserve twice the number of page table pages required to map all available memory.
    sz pt_bytes = 2 * PAGE_SIZE * (n_pts + n_pds + n_pdpts + n_pml4s);

    assert(dyn_addrs.len / 200 > pt_bytes); // Make sure we don't accidentally waste tons of memory on page tables.
    assert(pt_bytes < 16 * 0x100000); // Ensure pt pages are inside the mapped region (see _start).

    print_dbg(PINFO, STR("Paging with n_pages=%ld n_pts=%ld n_pds=%ld n_pdpts=%ld n_pml4s=%ld pt_bytes=0x%lx\n"),
              n_pages, n_pts, n_pds, n_pdpts, n_pml4s, pt_bytes);

    assert(cpu_has_pat()); // The cacheability controls implementation depends on this feature.

    global_pt_page_alloc = pool_new(byte_array_new((void *)dyn_addrs.vbase, pt_bytes), PAGE_SIZE);
    global_page_table.pml4 = pool_alloc(&global_pt_page_alloc);
    assert(global_page_table.pml4);

    // The translation constants must be set of before calling `pt_map` for the first time because
    // `pt_map` internally uses address translation.

    code_addrs.type = ADDR_MAPPING_TYPE_CANONICAL;
    code_addrs.mem_type = ADDR_MAPPING_MEMORY_DEFAULT;
    code_addrs.perms = PT_FLAG_RW;
    dyn_addrs.type = ADDR_MAPPING_TYPE_CANONICAL;
    dyn_addrs.mem_type = ADDR_MAPPING_MEMORY_DEFAULT;
    dyn_addrs.perms = PT_FLAG_RW;
    assert(!add_addr_mapping(code_addrs).is_error);
    assert(!add_addr_mapping(dyn_addrs).is_error);

    // Code and data
    for (sz offset = 0; offset < code_addrs.len; offset += PAGE_SIZE)
        assert(!pt_map(global_page_table, code_addrs.vbase + offset, code_addrs.pbase + offset, PT_FLAG_RW,
                       ADDR_MAPPING_MEMORY_DEFAULT)
                    .is_error);

    // Dynamic memory
    for (sz offset = 0; offset < dyn_addrs.len; offset += PAGE_SIZE)
        assert(!pt_map(global_page_table, dyn_addrs.vbase + offset, dyn_addrs.pbase + offset, PT_FLAG_RW,
                       ADDR_MAPPING_MEMORY_DEFAULT)
                    .is_error);

    write_cr3(result_paddr_t_checked(virt_to_phys((vaddr_t)global_page_table.pml4)));

    return byte_array_new((byte *)dyn_addrs.vbase + pt_bytes, dyn_addrs.len - pt_bytes);
}

struct result paging_map_region(struct addr_mapping addrs)
{
    struct result res = result_ok();

    res = add_addr_mapping(addrs);
    if (res.is_error)
        return res;

    for (sz offset = 0; offset < addrs.len; offset += PAGE_SIZE) {
        res = pt_map(global_page_table, addrs.vbase + offset, addrs.pbase + offset, addrs.perms, addrs.mem_type);
        if (res.is_error) {
            // Call unmap on all pages in case any of them have been mapped. Nothing bad will happen
            // if we call unmap on a page that has never been mapped.
            for (sz offset = 0; offset < addrs.len; offset += PAGE_SIZE)
                pt_unmap(global_page_table, addrs.vbase + offset);
            return res;
        }
    }

    return res;
}

struct result paging_unmap_region(struct addr_mapping addrs)
{
    struct result res = result_ok();

    for (sz offset = 0; offset < addrs.len; offset += PAGE_SIZE) {
        res = pt_unmap(global_page_table, addrs.vbase + offset);
        if (res.is_error)
            return res;
    }

    res = remove_addr_mapping(addrs);

    return res;
}
