#include <config.h>
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

static struct addr_mapping *add_addr_mapping(enum addr_mapping_type type, vaddr_t vbase, paddr_t pbase, sz len)
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
            assert(!intervals_overlap(vbase, vbase + len, mapping->vbase, mapping->vbase + mapping->len));

            // For physical addresses, we just count how many overlapping canonical and alias mappings
            // there are. See below for how these are used.
            if (intervals_overlap(pbase, pbase + len, mapping->pbase, mapping->pbase + mapping->len)) {
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
    assert(n_canonical == 1 || (n_canonical == 0 && n_alias == 1) || (n_canonical == 0 && n_alias == 0));

    // We now know the new mapping doesn't introduce any conflics. So let's create it!
    for (i32 i = 0; i < NUM_ADDR_MAPPINGS; i++) {
        if (!global_addr_mappings_used[i]) {
            global_addr_mappings[i].type = type;
            global_addr_mappings[i].vbase = vbase;
            global_addr_mappings[i].pbase = pbase;
            global_addr_mappings[i].len = len;
            global_addr_mappings_used[i] = true;
            return &global_addr_mappings[i];
        }
    }

    crash("Ran out of free address mappings\n");
}

// NOTE: Multiple virtual addresses can point to the same physical address. This function returns the
// virtual address (in high memory) that's used by the kernel to access the physical address. There may
// be mappings that use a different virtual address to access the same physical page.
static vaddr_t phys_to_virt(paddr_t paddr)
{
    if (!paddr)
        return 0; // To not have to check for NULL before calling this function.

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
        return canonical->vbase + (paddr - canonical->pbase); // Use the unique canonical mapping.
    else if (alias)
        return alias->vbase +
               (paddr - alias->pbase); // When there is no canonical mapping, the alias must be unique so we can use it.

    print_dbg(STR("Failed to translate paddr=0x%lx to a virtual address\n"), paddr);
    crash("Invalid address");
}

static paddr_t virt_to_phys(vaddr_t vaddr)
{
    if (!vaddr)
        return 0; // To not have to check for NULL before calling this function.

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
        return candidate->pbase + (vaddr - candidate->vbase);

    print_dbg(STR("Failed to translate vaddr=0x%lx to a physical address\n"), vaddr);
    crash("Invalid address");
}

///////////////////////////////////////////////////////////////////////////////
// Initialization                                                            //
///////////////////////////////////////////////////////////////////////////////

struct vma paging_init(struct addr_mapping code_addrs, struct addr_mapping dyn_addrs)
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

    print_dbg(STR("Paging with n_pages=%ld n_pts=%ld n_pds=%ld n_pdpts=%ld n_pml4s=%ld pt_bytes=0x%lx\n"), n_pages,
              n_pts, n_pds, n_pdpts, n_pml4s, pt_bytes);

    global_pt_page_alloc = pool_new(bytes_new((void *)dyn_addrs.vbase, pt_bytes), PAGE_SIZE);
    global_page_table.pml4 = pool_alloc(&global_pt_page_alloc);
    assert(global_page_table.pml4);

    // The translation constants must be set of before calling `pt_map` for the first time because
    // `pt_map` internally uses address translation.

    add_addr_mapping(ADDR_MAPPING_TYPE_CANONICAL, code_addrs.vbase, code_addrs.pbase, code_addrs.len);
    add_addr_mapping(ADDR_MAPPING_TYPE_CANONICAL, dyn_addrs.vbase, dyn_addrs.pbase, dyn_addrs.len);

    // Code and data
    for (sz offset = 0; offset < code_addrs.len; offset += PAGE_SIZE)
        assert(!pt_map(global_page_table, code_addrs.vbase + offset, code_addrs.pbase + offset, PT_FLAG_P | PT_FLAG_RW)
                    .is_error);

    // Dynamic memory
    for (sz offset = 0; offset < dyn_addrs.len; offset += PAGE_SIZE)
        assert(!pt_map(global_page_table, dyn_addrs.vbase + offset, dyn_addrs.pbase + offset, PT_FLAG_P | PT_FLAG_RW)
                    .is_error);

    write_cr3(virt_to_phys((vaddr_t)global_page_table.pml4));

    struct vma ret;
    ret.base = dyn_addrs.vbase + pt_bytes;
    ret.len = dyn_addrs.len - pt_bytes;
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
// Creating mappings and walking page tables                                 //
///////////////////////////////////////////////////////////////////////////////

struct page_table current_page_table(void)
{
    struct pt *pml4 = (struct pt *)phys_to_virt(read_cr3());
    // It does not make sense to return a value if the CR3 register and the global page table are out of sync.
    assert(pml4 == global_page_table.pml4);
    return global_page_table;
}

static inline paddr_t paddr_from_pte(struct pte pte)
{
    return pte.bits & ~(BIT(11) - 1);
}

static inline struct pte pte_from_paddr(paddr_t ptr, int flags)
{
    return (struct pte){ (ptr & ~(BIT(11) - 1)) | flags };
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
        return (struct pt *)phys_to_virt(paddr);
    }
    return NULL;
}

static struct pt *pt_get_or_alloc(struct pt *pt, sz idx, int flags)
{
    struct pt *ret;

    ret = pt_get(pt, idx);

    if (!ret) {
        ret = pool_alloc(&global_pt_page_alloc);
        if (!ret)
            return NULL;
        paddr_t paddr_ret = virt_to_phys((vaddr_t)ret);
        print_dbg(STR("Allocated page table page: vaddr=0x%lx paddr=0x%lx\n"), ret, paddr_ret);
        pt_insert(pt, idx, pte_from_paddr(paddr_ret, flags));
    } else {
        struct pte pte = pt->entries[idx]; // Safe because `pt_get` succeeded.
        // Update the existing PTE if the new flags are _more_ permissive than before.
        if ((flags & PT_FLAG_US) && !(pte.bits & PT_FLAG_US))
            pte.bits |= PT_FLAG_US;
        if ((flags & PT_FLAG_RW) && !(pte.bits & PT_FLAG_RW))
            pte.bits |= PT_FLAG_RW;
        pt_insert(pt, idx, pte);
    }

    return ret;
}

struct result pt_map(struct page_table page_table, vaddr_t vaddr, paddr_t paddr, int flags)
{
    struct pt *pdpt = pt_get_or_alloc(page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE), flags);
    if (!pdpt)
        return result_error(ENOMEM);
    struct pt *pd = pt_get_or_alloc(pdpt, PT_IDX(vaddr, PDPT_BIT_BASE), flags);
    if (!pd)
        return result_error(ENOMEM);
    struct pt *pt = pt_get_or_alloc(pd, PT_IDX(vaddr, PD_BIT_BASE), flags);
    if (!pd)
        return result_error(ENOMEM);
    sz pt_idx = PT_IDX(vaddr, PT_BIT_BASE);
    assert(0 <= pt_idx && pt_idx < countof(pt->entries));
    pt->entries[pt_idx] = pte_from_paddr(paddr, flags);
    return result_ok();
}

struct result_paddr_t pt_walk(struct page_table page_table, vaddr_t vaddr)
{
    struct pt *pdpt = pt_get(page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE));
    if (!pdpt)
        return result_paddr_t_error(EINVAL);
    struct pt *pd = pt_get(pdpt, PT_IDX(vaddr, PDPT_BIT_BASE));
    if (!pd)
        return result_paddr_t_error(EINVAL);
    struct pt *pt = pt_get(pd, PT_IDX(vaddr, PD_BIT_BASE));
    if (!pt)
        return result_paddr_t_error(EINVAL);
    sz pt_idx = PT_IDX(vaddr, PT_BIT_BASE);
    assert(0 <= pt_idx && pt_idx < countof(pt->entries));
    if (!(pt->entries[pt_idx].bits & PT_FLAG_P))
        return result_paddr_t_error(EINVAL);
    return result_paddr_t_ok(paddr_from_pte(pt->entries[pt_idx]) | (vaddr & (BIT(11) - 1)));
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

struct result pt_unmap(struct page_table page_table, vaddr_t vaddr)
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
    print_dbg(STR("Removed entry: pt_idx=%ld\n"), pt_idx);

    if (pt_is_empty(pt)) {
        print_dbg(STR("Freeing page table: pt=0x%lx\n"), pt);
        pd->entries[PT_IDX(vaddr, PD_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(&global_pt_page_alloc, pt);
    }
    if (pt_is_empty(pd)) {
        print_dbg(STR("Freeing page directory: pd=0x%lx\n"), pd);
        pdpt->entries[PT_IDX(vaddr, PDPT_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(&global_pt_page_alloc, pd);
    }
    if (pt_is_empty(pdpt)) {
        print_dbg(STR("Freeing page directory pointer table: pdpt=0x%lx\n"), pdpt);
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
        append_str(STR("    "), buf);
}

static struct result _pt_fmt(struct pt *pt, struct str_buf *buf, i16 level, i16 depth, vaddr_t base_vaddr)
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

struct result pt_fmt(struct page_table page_table, struct str_buf *buf, i16 depth)
{
    assert(buf);
    assert(depth <= 3);
    return _pt_fmt(page_table.pml4, buf, 0, depth, 0);
}

// TODO: Add facilities to allocate contiguous mappings with addr_mappings.

////////////////////////////////////////////////////////////////////////////////
// Managing VMAs (virtual memory areas)                                       //
////////////////////////////////////////////////////////////////////////////////

struct vma vma_new(vaddr_t base, sz len)
{
    struct vma vma;
    vma.base = base;
    vma.len = len;
    return vma;
}

struct vas vas_new(struct page_table pt, struct alloc alloc)
{
    struct vas vas;
    vas.pt = pt;
    vas.alloc = alloc;
    return vas;
}

struct result vas_map(struct vas vas, struct vma vma, int flags)
{
    assert(vma.len > 0);
    assert(vma.base <= PTR_MAX - vma.len);
    vaddr_t vaddr_page_end = ALIGN_UP(vma.base + vma.len, PAGE_SIZE);
    struct result res = result_ok();
    print_dbg(STR("Mapping VMA: base=0x%lx len=0x%lx\n"), vma.base, vma.len);
    for (vaddr_t vaddr = vma.base; vaddr < vaddr_page_end; vaddr += PAGE_SIZE) {
        struct result_paddr_t paddr_exist_res = pt_walk(vas.pt, vaddr);
        if (!paddr_exist_res.is_error) {
            print_dbg(STR("Skipping existing mapping: 0x%lx ---> 0x%lx\n"), vaddr,
                      result_paddr_t_checked(paddr_exist_res));
            continue;
        }
        paddr_t paddr = virt_to_phys((vaddr_t)alloc_alloc(vas.alloc, PAGE_SIZE, PAGE_SIZE));
        if (unlikely(!paddr))
            return result_error(ENOMEM);
        res = pt_map(vas.pt, vaddr, paddr, flags);
        if (unlikely(res.is_error))
            return res;
        print_dbg(STR("Mapped: 0x%lx ---> 0x%lx\n"), vaddr, paddr);
    }
    return res;
}

struct result vas_unmap(struct vas vas, struct vma vma)
{
    assert(vma.len > 0);
    assert(vma.base <= PTR_MAX - vma.len);
    vaddr_t vaddr_page_end = ALIGN_UP(vma.base + vma.len, PAGE_SIZE);
    struct result res = result_ok();
    print_dbg(STR("Unmapping VMA: base=0x%lx len=0x%lx\n"), vma.base, vma.len);
    for (vaddr_t vaddr = vma.base; vaddr < vaddr_page_end; vaddr += PAGE_SIZE) {
        struct result_paddr_t paddr = pt_walk(vas.pt, vaddr);
        if (unlikely(paddr.is_error))
            return result_error(paddr.code);
        alloc_free(vas.alloc, (void *)phys_to_virt(result_paddr_t_checked(paddr)), PAGE_SIZE);
        res = pt_unmap(vas.pt, vaddr);
        if (unlikely(res.is_error))
            return res;
        print_dbg(STR("Unmapped: 0x%lx -/-> 0x%lx\n"), vaddr, paddr);
    }
    return res;
}

struct result vas_memcpy(struct vas vas, struct vma vma, struct bytes src)
{
    assert(vma.len > 0);
    assert(vma.base <= PTR_MAX - vma.len);
    assert(src.len <= vma.len);

    vaddr_t vaddr_end = vma.base + vma.len;
    vaddr_t vaddr_page_end = ALIGN_UP(vaddr_end, PAGE_SIZE);

    sz offset = 0;
    print_dbg(STR("Copying to VMA: base=0x%lx len=0x%lx src.dat=0x%lx src.len=%ld\n"), vma.base, vma.len, src.dat,
              src.len);

    for (vaddr_t vaddr = vma.base; vaddr < vaddr_page_end && offset < src.len;
         vaddr += PAGE_SIZE, offset += PAGE_SIZE) {
        // There is some pointer laundering going on here. First, we walk the page table of the VAS (which is
        // not necessarily the active page table) to find the physical address of the current virtual address.
        // This virtual address could be from a different virtual memory area than the default kernel area. But
        // the physical address will be accessible from some kernel virtual address (because this is true for all
        // physical pages we allocate). Thus, by passing the physical address to `phys_to_virt`, we get a kernel
        // virtual address that can be used to write to the requested page in the VMA.
        struct result_paddr_t paddr = pt_walk(vas.pt, vaddr);
        if (unlikely(paddr.is_error))
            return result_error(EINVAL);
        void *dest = (void *)phys_to_virt(result_paddr_t_checked(paddr));
        memcpy(bytes_new(dest, MIN(vaddr_end - vaddr, PAGE_SIZE)),
               bytes_new(src.dat + offset, MIN(src.len - offset, PAGE_SIZE)));
    }

    return result_ok();
}

struct result vas_memset(struct vas vas, struct vma vma, byte value)
{
    assert(vma.len > 0);
    assert(vma.base <= PTR_MAX - vma.len);

    vaddr_t vaddr_end = vma.base + vma.len;
    vaddr_t vaddr_page_end = ALIGN_UP(vaddr_end, PAGE_SIZE);

    print_dbg(STR("Setting VMA: base=0x%lx len=0x%lx value=%hhu\n"), vma.base, vma.len, value);

    for (vaddr_t vaddr = vma.base; vaddr < vaddr_page_end; vaddr += PAGE_SIZE) {
        // See `vas_memcpy` for an explanation of the pointer laundering.
        struct result_paddr_t paddr = pt_walk(vas.pt, vaddr);
        if (unlikely(paddr.is_error))
            return result_error(EINVAL);
        void *dest = (void *)phys_to_virt(result_paddr_t_checked(paddr));
        memset(bytes_new(dest, MIN(vaddr_end - vaddr, PAGE_SIZE)), value);
    }

    return result_ok();
}
