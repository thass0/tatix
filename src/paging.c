#include <config.h>
#include <tx/paging.h>

static struct pool *pt_alloc_init(sz n_pages, struct arena *arn)
{
    assert(n_pages > 0);
    struct pool *pool = arena_alloc(arn, sizeof(*pool));
    void *buf = arena_alloc_aligned_array(arn, n_pages, PAGE_SIZE, PAGE_SIZE);
    assert(buf);
    assert(n_pages <= SZ_MAX / PAGE_SIZE);
    *pool = pool_new(bytes_new(buf, n_pages * PAGE_SIZE), PAGE_SIZE);
    return pool;
}

struct page_table pt_alloc_page_table(struct arena *arn)
{
    struct page_table pt;
    pt.pt_alloc = pt_alloc_init(32, arn);
    pt.pml4 = pool_alloc(pt.pt_alloc);
    return pt;
}

///////////////////////////////////////////////////////////////////////////////
// Creating mappings and walking page tables                                 //
///////////////////////////////////////////////////////////////////////////////

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

static struct pt *pt_get(struct pt *pt, sz idx)
{
    assert(pt);
    assert(0 <= idx && idx < countof(pt->entries));

    if (pt->entries[idx].bits & PT_FLAG_P)
        return (struct pt *)paddr_from_pte(pt->entries[idx]);
    return NULL;
}

static struct pt *pt_get_or_alloc(struct page_table page_table, struct pt *pt, sz idx)
{
    struct pt *ret;

    ret = pt_get(pt, idx);

    if (!ret) {
        ret = pool_alloc(page_table.pt_alloc);
        if (!ret)
            return NULL;
        print_dbg(STR("Allocated page table page: 0x%lx\n"), ret);
        pt_insert(pt, idx, pte_from_paddr((paddr_t)ret, PT_FLAG_P | PT_FLAG_RW));
    }

    return ret;
}

int pt_map(struct page_table page_table, vaddr_t vaddr, paddr_t paddr)
{
    struct pt *pdpt = pt_get_or_alloc(page_table, page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE));
    if (!pdpt)
        return -1;
    struct pt *pd = pt_get_or_alloc(page_table, pdpt, PT_IDX(vaddr, PDPT_BIT_BASE));
    if (!pd)
        return -1;
    struct pt *pt = pt_get_or_alloc(page_table, pd, PT_IDX(vaddr, PD_BIT_BASE));
    if (!pd)
        return -1;
    sz pt_idx = PT_IDX(vaddr, PT_BIT_BASE);
    assert(0 <= pt_idx && pt_idx < countof(pt->entries));
    pt->entries[pt_idx] = pte_from_paddr(paddr, PT_FLAG_P | PT_FLAG_RW);
    print_dbg(STR("Created entry: pt_idx=%ld\n"), pt_idx);
    return 0;
}

paddr_t pt_walk(struct page_table page_table, vaddr_t vaddr)
{
    struct pt *pdpt = pt_get(page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE));
    if (!pdpt)
        return PADDR_INVALID;
    struct pt *pd = pt_get(pdpt, PT_IDX(vaddr, PDPT_BIT_BASE));
    if (!pd)
        return PADDR_INVALID;
    struct pt *pt = pt_get(pd, PT_IDX(vaddr, PD_BIT_BASE));
    if (!pt)
        return PADDR_INVALID;
    sz pt_idx = PT_IDX(vaddr, PT_BIT_BASE);
    assert(0 <= pt_idx && pt_idx < countof(pt->entries));
    if (!(pt->entries[pt_idx].bits & PT_FLAG_P))
        return PADDR_INVALID;
    return paddr_from_pte(pt->entries[pt_idx]) | (vaddr & (BIT(11) - 1));
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

int pt_unmap(struct page_table page_table, vaddr_t vaddr)
{
    struct pt *pdpt = pt_get(page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE));
    if (!pdpt)
        return -1;
    struct pt *pd = pt_get(pdpt, PT_IDX(vaddr, PDPT_BIT_BASE));
    if (!pd)
        return -1;
    struct pt *pt = pt_get(pd, PT_IDX(vaddr, PD_BIT_BASE));
    if (!pt)
        return -1;
    sz pt_idx = PT_IDX(vaddr, PT_BIT_BASE);
    assert(0 <= pt_idx && pt_idx < countof(pt->entries));
    if (!(pt->entries[pt_idx].bits & PT_FLAG_P))
        return -1;
    pt->entries[pt_idx].bits &= ~PT_FLAG_P;
    print_dbg(STR("Removed entry: pt_idx=%ld\n"), pt_idx);

    if (pt_is_empty(pt)) {
        print_dbg(STR("Freeing page table: pt=0x%lx\n"), pt);
        pd->entries[PT_IDX(vaddr, PD_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(page_table.pt_alloc, pt);
    }
    if (pt_is_empty(pd)) {
        print_dbg(STR("Freeing page directory: pd=0x%lx\n"), pd);
        pdpt->entries[PT_IDX(vaddr, PDPT_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(page_table.pt_alloc, pd);
    }
    if (pt_is_empty(pdpt)) {
        print_dbg(STR("Freeing page directory pointer table: pdpt=0x%lx\n"), pdpt);
        page_table.pml4->entries[PT_IDX(vaddr, PML4_BIT_BASE)].bits &= ~PT_FLAG_P;
        pool_free(page_table.pt_alloc, pdpt);
    }

    return 0;
}

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

struct vas vas_init(struct page_table pt, struct buddy *phys_alloc)
{
    assert(phys_alloc);
    struct vas vas;
    vas.pt = pt;
    vas.phys_alloc = phys_alloc;
    return vas;
}

int vas_map(struct vas vas, struct vma vma)
{
    assert(vma.len > 0);
    assert(IS_ALIGNED(vma.len, PAGE_SIZE));
    assert(vma.base <= PTR_MAX - vma.len);
    vaddr_t vaddr_end = vma.base + vma.len;
    int rc = 0;
    print_dbg(STR("Mapping VMA: base=0x%lx len=0x%lx\n"), vma.base, vma.len);
    for (vaddr_t vaddr = vma.base; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
        paddr_t paddr = (paddr_t)buddy_alloc(vas.phys_alloc, 0);
        if (unlikely(!paddr))
            return -1;
        rc = pt_map(vas.pt, vaddr, paddr);
        if (unlikely(rc < 0))
            return rc;
        print_dbg(STR("Mapped: 0x%lx ---> 0x%lx\n"), vaddr, paddr);
    }
    return 0;
}

int vas_unmap(struct vas vas, struct vma vma)
{
    assert(vma.len > 0);
    assert(IS_ALIGNED(vma.len, PAGE_SIZE));
    assert(vma.base <= PTR_MAX - vma.len);
    vaddr_t vaddr_end = vma.base + vma.len;
    int rc = 0;
    print_dbg(STR("Unmapping VMA: base=0x%lx len=0x%lx\n"), vma.base, vma.len);
    for (vaddr_t vaddr = vma.base; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
        paddr_t paddr = pt_walk(vas.pt, vaddr);
        if (unlikely(paddr == PADDR_INVALID))
            return -1;
        buddy_free(vas.phys_alloc, (void *)paddr, 0);
        rc = pt_unmap(vas.pt, vaddr);
        if (unlikely(rc < 0))
            return rc;
        print_dbg(STR("Unmapped: 0x%lx -/-> 0x%lx\n"), vaddr, paddr);
    }
    return 0;
}
