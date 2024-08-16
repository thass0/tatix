#ifndef __TX_PORTS_H__
#define __TX_PORTS_H__

#include <tx/asm.h>
#include <tx/base.h>

#define PIC1_CMD_PORT 0x20
#define PIC1_DAT_PORT 0x21
#define PIC2_CMD_PORT 0xa0
#define PIC2_DAT_PORT 0xa1

#define PIC2_IRQ 2

// PIC command codes
#define PIC_EOI_CMD 0x20 // "End of Interrupt"
#define PIC_INIT_CMD 0x11 // "Initialize"

#define PIC_ICW4_8086_MODE 0x01

static inline void pic_sent_eoi(u8 irq)
{
    if (irq >= 8)
        outb(PIC2_CMD_PORT, PIC_EOI_CMD);
    outb(PIC1_CMD_PORT, PIC_EOI_CMD);
}

static inline void pic_remap(u8 pic1_vec_base, u8 pic2_vec_base)
{
    u8 mask1 = inb(PIC1_DAT_PORT);
    u8 mask2 = inb(PIC2_DAT_PORT);

    outb(PIC1_CMD_PORT, PIC_INIT_CMD);
    outb(PIC1_DAT_PORT, pic1_vec_base);
    outb(PIC1_DAT_PORT, 1 << PIC2_IRQ);
    outb(PIC1_DAT_PORT, PIC_ICW4_8086_MODE);

    outb(PIC2_DAT_PORT, PIC_INIT_CMD);
    outb(PIC2_DAT_PORT, pic2_vec_base);
    outb(PIC2_DAT_PORT, PIC2_IRQ);
    outb(PIC2_DAT_PORT, PIC_ICW4_8086_MODE);

    outb(PIC1_DAT_PORT, mask1);
    outb(PIC1_DAT_PORT, mask2);
}

#endif // __TX_PORTS_H__
