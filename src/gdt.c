#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/gdt.h>
// #include <tx/bytes.h>

// The last _two_ entries in this array make up a single 16-byte TSS descriptor.
static __aligned(16) struct seg_descriptor gdt[7];

static __aligned(8) struct task_state ts;

static void gdt_init_seg_descriptor(struct seg_descriptor *desc, u8 type, u8 dpl)
{
    assert(desc);
    desc->limit_low = 0xffff;
    desc->base_low = 0;
    desc->base_mid = 0;
    desc->attr |= type;
    desc->attr |= SEG_DESC_FLAG_S;
    desc->attr |= dpl << SEG_DESC_DPL_SHIFT;
    desc->attr |= SEG_DESC_FLAG_P;
    desc->attr |= 0xf << SEG_DESC_LIMIT_HIGH_SHIFT;
    desc->attr |= SEG_DESC_FLAG_L;
    desc->attr &= ~SEG_DESC_FLAG_D;
    desc->attr |= SEG_DESC_FLAG_G;
    desc->base_high = 0;
}

static void gdt_init_tss_descriptor(struct task_state *ts, sz limit, struct seg_descriptor *low,
                                    struct seg_descriptor *high)
{
    assert(ts);
    assert(low);
    assert(high);
    low->limit_low = limit & 0xffff;
    low->base_low = (ptr)ts & 0xffff;
    low->base_mid = ((ptr)ts >> 16) & 0xff;
    low->attr |= SEG_DESC_TYPE_TSS;
    low->attr |= SEG_DESC_DPL_KERN << SEG_DESC_DPL_SHIFT;
    low->attr |= SEG_DESC_FLAG_P;
    low->attr |= ((limit >> 16) & 0xff) << SEG_DESC_LIMIT_HIGH_SHIFT;
    low->attr |= SEG_DESC_FLAG_G;
    low->base_high = ((ptr)ts >> 24) & 0xff;
    // The 64-bit TSS descriptor is 16 byte, not 8 like normal descriptors. The upper
    // four bytes are either reserved or zero. The four bytes below that are the upper
    // four bytes of the base.
    *((u32 *)high) = ((ptr)ts >> 32) & 0xffffffff;
}

void gdt_init(void)
{
    volatile struct seg_pseudo_descriptor_64 gdtr;

    gdt_init_seg_descriptor(&gdt[SEG_IDX_KERN_CODE], SEG_DESC_TYPE_CODE_RX, SEG_DESC_DPL_KERN);
    gdt_init_seg_descriptor(&gdt[SEG_IDX_KERN_DATA], SEG_DESC_TYPE_DATA_RW, SEG_DESC_DPL_KERN);
    gdt_init_seg_descriptor(&gdt[SEG_IDX_USER_CODE], SEG_DESC_TYPE_CODE_RX, SEG_DESC_DPL_USER);
    gdt_init_seg_descriptor(&gdt[SEG_IDX_USER_DATA], SEG_DESC_TYPE_DATA_RW, SEG_DESC_DPL_USER);
    static_assert(sizeof(ts));

    ts.io_map_base = 0xffff;
    gdt_init_tss_descriptor(&ts, sizeof(ts) - 1, &gdt[SEG_IDX_TSS], &gdt[SEG_IDX_TSS + 1]);

    static_assert(sizeof(gdt));
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)&gdt[0];
    lgdt(&gdtr);

    ltr(segment_selector(SEG_IDX_TSS, SEG_DESC_DPL_KERN));
}

struct task_state *tss_get_global(void)
{
    return &ts;
}
