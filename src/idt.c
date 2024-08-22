#include <tx/asm.h>
#include <tx/base.h>
#include <tx/isr.h>
#include <tx/pic.h>

// 64-bit Interrupt Descriptor Table

struct idt_entry {
    u16 offset1;
    u16 seg_selector; // Code segment selector for the handler routine
    u8 ist; // 3-bit Interrupt Stack Table (IST) index field; upper 5 bits are 0
    u8 attributes;
    u16 offset2;
    u32 offset3;
    u32 reserved;
} __packed;

struct idtr {
    u16 limit;
    u64 base;
} __packed;

#define GATE_TYPE_INTERRUPT 0xe
#define GATE_TYPE_TRAP 0xf
#define GATE_PRESENT_FLAG_BIT 7
#define ATTR_INTERRUPT_GATE (GATE_TYPE_INTERRUPT | (1 << GATE_PRESENT_FLAG_BIT))
#define NUM_IDT_ENTRIES 256

__aligned(16) static struct idt_entry idt[NUM_IDT_ENTRIES];

void init_idt_entry(struct idt_entry *ent, ptr handler, u8 attributes)
{
    ent->offset1 = (u16)(handler & 0xffff);
    ent->seg_selector = BOOT_GDT_CODE_DESC;
    ent->ist = 0; // Disable the use of the IST, plus set the reserved bits to 0
    ent->attributes = attributes;
    ent->offset2 = (u16)((handler >> 16) & 0xffff);
    ent->offset3 = (u32)((handler >> 32) & 0xffffffff);
    ent->reserved = 0;
}

void init_idt(void)
{
    struct idtr idtr;
    idtr.limit = sizeof(struct idt_entry) * NUM_IDT_ENTRIES - 1;
    idtr.base = (u64)idt;

    for (i32 i = 0; i < NUM_IDT_ENTRIES; i++)
        init_idt_entry(&idt[i], 0, 0);

    init_idt_entry(&idt[0], (ptr)isr_stub_0, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[1], (ptr)isr_stub_1, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[2], (ptr)isr_stub_2, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[3], (ptr)isr_stub_3, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[4], (ptr)isr_stub_4, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[5], (ptr)isr_stub_5, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[6], (ptr)isr_stub_6, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[7], (ptr)isr_stub_7, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[8], (ptr)isr_stub_8, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[9], (ptr)isr_stub_9, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[10], (ptr)isr_stub_10, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[11], (ptr)isr_stub_11, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[12], (ptr)isr_stub_12, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[13], (ptr)isr_stub_13, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[14], (ptr)isr_stub_14, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[15], (ptr)isr_stub_15, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[16], (ptr)isr_stub_16, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[17], (ptr)isr_stub_17, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[18], (ptr)isr_stub_18, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[19], (ptr)isr_stub_19, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[20], (ptr)isr_stub_20, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[21], (ptr)isr_stub_21, ATTR_INTERRUPT_GATE);

    init_idt_entry(&idt[32], (ptr)isr_stub_32, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[33], (ptr)isr_stub_33, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[34], (ptr)isr_stub_34, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[35], (ptr)isr_stub_35, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[36], (ptr)isr_stub_36, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[37], (ptr)isr_stub_37, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[38], (ptr)isr_stub_38, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[39], (ptr)isr_stub_39, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[40], (ptr)isr_stub_40, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[41], (ptr)isr_stub_41, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[42], (ptr)isr_stub_42, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[43], (ptr)isr_stub_43, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[44], (ptr)isr_stub_44, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[45], (ptr)isr_stub_45, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[46], (ptr)isr_stub_46, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[47], (ptr)isr_stub_47, ATTR_INTERRUPT_GATE);

    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void interrupt_init(void)
{
    disable_interrupts();
    pic_remap(IRQ_VECTORS_BEG, IRQ_VECTORS_BEG + 8);
    init_idt();
    enable_interrupts();
}
