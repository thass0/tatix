#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/gdt.h>

static __aligned(16) struct seg_descriptor gdt[5];

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

void gdt_init(void)
{
    volatile struct seg_pseudo_descriptor_64 gdtr;

    gdt_init_seg_descriptor(&gdt[SEG_IDX_KERN_CODE], SEG_DESC_TYPE_CODE_RX, SEG_DESC_DPL_KERN);
    gdt_init_seg_descriptor(&gdt[SEG_IDX_KERN_DATA], SEG_DESC_TYPE_DATA_RW, SEG_DESC_DPL_KERN);
    gdt_init_seg_descriptor(&gdt[SEG_IDX_USER_CODE], SEG_DESC_TYPE_CODE_RX, SEG_DESC_DPL_USER);
    gdt_init_seg_descriptor(&gdt[SEG_IDX_USER_DATA], SEG_DESC_TYPE_DATA_RW, SEG_DESC_DPL_USER);

    static_assert(sizeof(gdt));
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)&gdt[0];
    lgdt(&gdtr);
}
