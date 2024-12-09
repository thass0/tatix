#include <config.h>
#include <tx/fmt.h>
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

struct page_table current_page_table(void)
{
    struct page_table pt;
    u64 pml4;
    pt.pt_alloc = NULL;
    __asm__ volatile("mov %%cr3, %0" : "=r"(pml4));
    pt.pml4 = (struct pt *)pml4;
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

static struct pt *pt_get_or_alloc(struct page_table page_table, struct pt *pt, sz idx, int flags)
{
    struct pt *ret;

    ret = pt_get(pt, idx);

    if (!ret) {
        ret = pool_alloc(page_table.pt_alloc);
        if (!ret)
            return NULL;
        // NOTE: The memory in the pool for the page table pages must already be mapped,
        // meaning `pt_walk` won't return an error here.
        paddr_t paddr_ret = result_paddr_t_checked(pt_walk(current_page_table(), (vaddr_t)ret));
        print_dbg(STR("Allocated page table page: vaddr=0x%lx paddr=0x%lx\n"), ret, paddr_ret);
        pt_insert(pt, idx, pte_from_paddr(paddr_ret, flags));
    }

    return ret;
}

struct result pt_map(struct page_table page_table, vaddr_t vaddr, paddr_t paddr, int flags)
{
    struct pt *pdpt = pt_get_or_alloc(page_table, page_table.pml4, PT_IDX(vaddr, PML4_BIT_BASE), flags);
    if (!pdpt)
        return result_error(ENOMEM);
    struct pt *pd = pt_get_or_alloc(page_table, pdpt, PT_IDX(vaddr, PDPT_BIT_BASE), flags);
    if (!pd)
        return result_error(ENOMEM);
    struct pt *pt = pt_get_or_alloc(page_table, pd, PT_IDX(vaddr, PD_BIT_BASE), flags);
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

struct vas vas_new(struct page_table pt, struct buddy *phys_alloc)
{
    assert(phys_alloc);
    struct vas vas;
    vas.pt = pt;
    vas.phys_alloc = phys_alloc;
    return vas;
}

struct result vas_map(struct vas vas, struct vma vma, int flags)
{
    assert(vma.len > 0);
    assert(IS_ALIGNED(vma.len, PAGE_SIZE));
    assert(vma.base <= PTR_MAX - vma.len);
    vaddr_t vaddr_end = vma.base + vma.len;
    struct result res = result_ok();
    print_dbg(STR("Mapping VMA: base=0x%lx len=0x%lx\n"), vma.base, vma.len);
    for (vaddr_t vaddr = vma.base; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
        paddr_t paddr = (paddr_t)buddy_alloc(vas.phys_alloc, 0);
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
    assert(IS_ALIGNED(vma.len, PAGE_SIZE));
    assert(vma.base <= PTR_MAX - vma.len);
    vaddr_t vaddr_end = vma.base + vma.len;
    struct result res = result_ok();
    print_dbg(STR("Unmapping VMA: base=0x%lx len=0x%lx\n"), vma.base, vma.len);
    for (vaddr_t vaddr = vma.base; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
        struct result_paddr_t paddr = pt_walk(vas.pt, vaddr);
        if (unlikely(paddr.is_error))
            return result_error(paddr.code);
        buddy_free(vas.phys_alloc, (void *)result_paddr_t_checked(paddr), 0);
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
    assert(IS_ALIGNED(vma.len, PAGE_SIZE));
    assert(vma.base <= PTR_MAX - vma.len);
    assert(src.len <= vma.len);

    vaddr_t vaddr_end = vma.base + vma.len;
    sz offset = 0;
    print_dbg(STR("Copying to VMA: base=0x%lx len=0x%lx src.dat=0x%lx src.len=%ld\n"), vma.base, vma.len, src.dat,
              src.len);

    for (vaddr_t vaddr = vma.base; vaddr < vaddr_end && offset < src.len; vaddr += PAGE_SIZE, offset += PAGE_SIZE) {
        struct result_paddr_t paddr = pt_walk(vas.pt, vaddr);
        if (unlikely(paddr.is_error))
            return result_error(EINVAL);
        void *dest = (void *)result_paddr_t_checked(paddr);
        memcpy(bytes_new(dest, PAGE_SIZE),
               bytes_new(src.dat + offset, src.len - offset >= PAGE_SIZE ? PAGE_SIZE : src.len - offset));
    }

    return result_ok();
}
