#include <config.h>
#include <tx/paging.h>

static struct pool pt_alloc_init(sz n_pages, struct arena *arn)
{
    assert(n_pages > 0);
    void *buf = arena_alloc_aligned_array(arn, n_pages, PAGE_SIZE, PAGE_SIZE);
    assert(buf);
    assert(n_pages <= SZ_MAX / PAGE_SIZE);
    return pool_new(bytes_new(buf, n_pages * PAGE_SIZE), PAGE_SIZE);
}

struct page_table *pt_alloc_page_table(struct arena *arn)
{
    struct page_table *pt = arena_alloc(arn, sizeof(*pt));
    pt->pt_alloc = pt_alloc_init(32, arn);
    pt->pml4 = pool_alloc(&pt->pt_alloc);
    return pt;
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

static struct pt *pt_get(struct pt *pt, sz idx)
{
    assert(pt);
    assert(0 <= idx && idx < countof(pt->entries));

    if (pt->entries[idx].bits & PT_FLAG_P)
        return (struct pt *)paddr_from_pte(pt->entries[idx]);
    return NULL;
}

static struct pt *pt_get_or_alloc(struct page_table *page_table, struct pt *pt, sz idx)
{
    struct pt *ret;

    ret = pt_get(pt, idx);

    if (!ret) {
        ret = pool_alloc(&page_table->pt_alloc);
        if (!ret)
            return NULL;
        pt_insert(pt, idx, pte_from_paddr((paddr_t)ret, PT_FLAG_P | PT_FLAG_RW));
    }

    return ret;
}

int pt_map(struct page_table *page_table, vaddr_t vaddr, paddr_t paddr)
{
    assert(page_table);
    struct pt *pdpt = pt_get_or_alloc(page_table, page_table->pml4, PT_IDX(vaddr, PML4_BIT_BASE));
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
    return 0;
}

int pt_walk(struct page_table *page_table, vaddr_t vaddr, paddr_t *paddr_ret)
{
    assert(page_table);
    struct pt *pdpt = pt_get(page_table->pml4, PT_IDX(vaddr, PML4_BIT_BASE));
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
    *paddr_ret = paddr_from_pte(pt->entries[pt_idx]) | (vaddr & (BIT(11) - 1));
    return 0;
}
