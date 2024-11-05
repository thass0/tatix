#ifndef __TX_GDT_H__
#define __TX_GDT_H__

#include <tx/base.h>

struct seg_selector {
    u16 rpl : 2;
    u16 table_indicator : 1;
    u16 table_index : 13;
} __packed;

static_assert(sizeof(struct seg_selector) == 2);

struct seg_descriptor {
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u16 attr;
    u8 base_high;
} __packed;

static_assert(sizeof(struct seg_descriptor) == 8);

#define SEG_DESC_TYPE_DATA_RW 2
#define SEG_DESC_TYPE_CODE_RX 10
#define SEG_DESC_TYPE_TSS 9
#define SEG_DESC_FLAG_S BIT(4)
#define SEG_DESC_DPL_SHIFT 5
#define SEG_DESC_DPL_USER 3
#define SEG_DESC_DPL_KERN 0
#define SEG_DESC_FLAG_P BIT(7)
#define SEG_DESC_LIMIT_HIGH_SHIFT 8
#define SEG_DESC_FLAG_L BIT(13)
#define SEG_DESC_FLAG_D BIT(14)
#define SEG_DESC_FLAG_G BIT(15)

struct seg_pseudo_descriptor_64 {
    u16 limit;
    u64 base;
} __packed;

static_assert(sizeof(struct seg_pseudo_descriptor_64) == 10);

#define SEG_IDX_KERN_CODE 1
#define SEG_IDX_KERN_DATA 2
#define SEG_IDX_USER_CODE 3
#define SEG_IDX_USER_DATA 4
#define SEG_IDX_TSS 5

// Switch to a 64-bit GDT that defines a code segment for kernel mode and a code
// segment for user mode. The DS and SS registers aren't used in 64-bit mode so
// we don't need data segments (see IA-32 manual, section 3.4.4).
void gdt_init(void);

struct seg_descriptor *gdt_get_global(void);

struct task_state {
    u32 _reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 _reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 _reserved2;
    u16 _reserved3;
    u16 io_map_base;
} __packed;

static_assert(sizeof(struct task_state) == 104);

struct task_state *tss_get_global(void);

static inline u16 segment_selector(u16 gdt_idx, u16 rpl)
{
    return (gdt_idx << 3) | rpl;
}

#endif // __TX_GDT_H__
