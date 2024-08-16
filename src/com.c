// Driver for x86 COM ports. The underlying device is 16550 or 8250 compatible.

#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/com.h>

#define OFFSET_RX 0
#define OFFSET_TX 0
#define OFFSET_DIVISOR_LOW 0
#define OFFSET_DIVISOR_HIGH 1
#define OFFSET_INTERRUPT_ENABLE 1
#define OFFSET_INTERRUPT_ID 2
#define OFFSET_FIFO_CONTROL 2
#define OFFSET_LINE_CONTROL 3
#define OFFSET_MODEM_CONTROL 4
#define OFFSET_LINE_STATUS 5
#define OFFSET_MODEM_STATUS 6
#define OFFSET_SCRATCH 7

#define LINE_CONTROL_DATA_LOW BIT(0)
#define LINE_CONTROL_DATA_HIGH BIT(1)
#define LINE_CONTROL_STOP BIT(2)
#define LINE_CONTROL_DLAB BIT(7)

#define MODEM_CONTROL_LOOP BIT(4)

#define LINE_STATUS_RX_READY BIT(0)
#define LINE_STATUS_TX_READY BIT(5)

// The smallest possible divisor makes for the biggest possible baud rate.
#define DIV_LOW 1
#define DIV_HIGH 0

int com_init(u16 port)
{
    // Setup Line Control Register
    u8 lcr = inb(port + OFFSET_LINE_CONTROL);
    lcr |= LINE_CONTROL_DLAB;
    outb(port + OFFSET_LINE_CONTROL, lcr);
    outb(port + OFFSET_DIVISOR_LOW, DIV_LOW);
    outb(port + OFFSET_DIVISOR_HIGH, DIV_HIGH);
    lcr &= ~LINE_CONTROL_DLAB;
    lcr |= LINE_CONTROL_DATA_LOW;
    lcr |= LINE_CONTROL_DATA_HIGH;
    lcr |= LINE_CONTROL_STOP;
    outb(port + OFFSET_LINE_CONTROL, lcr);

    // Disables interrupts
    outb(port + OFFSET_INTERRUPT_ENABLE, 0);

    // Test if the setup works
    outb(port + OFFSET_MODEM_CONTROL, MODEM_CONTROL_LOOP);
    outb(port, 0xbe);
    if (inb(port) != 0xbe)
        return -1;
    outb(port, 0xff);
    if (inb(port) != 0xff)
        return -1;

    outb(port + OFFSET_MODEM_CONTROL, 0);
    return 0;
}

int com_write(u16 port, struct str str)
{
    assert(str.dat);

    if (str.len <= 0)
        return -1;

    while (str.len--) {
        while (!(inb(port + OFFSET_LINE_STATUS) & LINE_STATUS_TX_READY))
            ;
        outb(port, *str.dat++);
    }

    return 0;
}

int com_read(u16 port, struct str_buf *buf)
{
    sz len = 0;

    assert(buf);
    assert(buf->dat);

    if (buf->cap <= 0)
        return -1;

    while (len < buf->cap) {
        while (!(inb(port + OFFSET_LINE_STATUS) & LINE_STATUS_RX_READY))
            ;
        buf->dat[len++] = inb(port);
    }

    buf->len = len;
    return 0;
}
