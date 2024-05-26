#include <base.h>
#include <isr.h>
#include <pic.h>

// 64-bit Interrupt Descriptor Table

struct idt_entry {
    u16 offset1;
    u16 seg_selector; // Code segment selector for the handler routine
    u8 ist; // 3-bit Interrupt Stack Table (IST) index field; upper 5 bits are 0
    u8 attributes;
    u16 offset2;
    u32 offset3;
    u32 reserved;
} __attribute__((packed));

struct idtr {
    u16 limit;
    u64 base;
} __attribute__((packed));

#define GATE_TYPE_INTERRUPT 0xe
#define GATE_TYPE_TRAP 0xf
#define GATE_PRESENT_FLAG_BIT 7
#define ATTR_INTERRUPT_GATE (GATE_TYPE_INTERRUPT | (1 << GATE_PRESENT_FLAG_BIT))
#define NUM_IDT_ENTRIES 256
#define NUM_RESERVED_VECTORS 32

#define IRQ_VECTORS_BASE NUM_RESERVED_VECTORS

extern u16 GDT64_CODE_SEG_SELECTOR;

__attribute__((aligned(16)))
static struct idt_entry idt[NUM_IDT_ENTRIES];

void init_idt_entry(struct idt_entry *ent, ptr handler, u8 attributes)
{
    ent->offset1 = (u16)(handler & 0xffff);
    ent->seg_selector = GDT64_CODE_SEG_SELECTOR;
    ent->ist = 0;               // Disable the use of the IST, plus set the reserved bits to 0
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

    for (i32 i = 0; i < NUM_IDT_ENTRIES; i++) {
        idt[i].offset1 = 0;
        idt[i].seg_selector = 0;
        idt[i].ist = 0;
        idt[i].attributes = 0;
        idt[i].offset2 = 0;
        idt[i].offset3 = 0;
        idt[i].reserved = 0;
    }

    for (i32 i = 0; i < NUM_RESERVED_VECTORS; i++)
        init_idt_entry(&idt[i], (ptr)handle_invalid_interrupt, ATTR_INTERRUPT_GATE);

    init_idt_entry(&idt[32], (ptr)handle_keyboard_interrupt, ATTR_INTERRUPT_GATE);
    init_idt_entry(&idt[33], (ptr)handle_example_interrupt, ATTR_INTERRUPT_GATE);

    __asm__ volatile ("lidt %0" : : "m"(idtr));
    __asm__ volatile ("sti");
}

void init_interrupts(void)
{
    __asm__ volatile ("cli");
    pic_remap(IRQ_VECTORS_BASE, IRQ_VECTORS_BASE + 8);
    init_idt();
}
