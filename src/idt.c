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

extern ptr isr_stub_reserved_table[22];
extern ptr isr_stub_irq_table[15];

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

    for (i32 i = 0; i < NUM_USED_RESERVED_VECTORS; i++)
        init_idt_entry(&idt[i], isr_stub_reserved_table[i], ATTR_INTERRUPT_GATE);

    for (i32 i = 0; i < NUM_IRQ_VECTORS; i++)
        init_idt_entry(&idt[NUM_RESERVED_VECTORS + i], isr_stub_irq_table[i], ATTR_INTERRUPT_GATE);

    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void interrupt_init(void)
{
    disable_interrupts();
    pic_remap(IRQ_VECTORS_BEG, IRQ_VECTORS_BEG + 8);
    init_idt();
    enable_interrupts();
}
