// Driver for Intel 82540EM (called e1000 in Linux and QEMU).

#include <tx/asm.h>
#include <tx/base.h>
#include <tx/byte.h>
#include <tx/isr.h>
#include <tx/kvalloc.h>
#include <tx/net/netdev.h>
#include <tx/net/send_buf.h>
#include <tx/paging.h>
#include <tx/pci.h>
#include <tx/pic.h>
#include <tx/print.h>

// The 8254x PCI/PCI-X Family of Gigabit Ethernet Controllers Software Developer’s Manual (2009 version) was used as a
// source for this driver References to sections are with respect to this document. A copy of the manual used can be
// found in the manuals/ directory. It could also be found here at the time of writing:
// https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf

// Reference: Table 13-2. Ethernet Controller Register Summary.
#define E1000_OFFSET_CTRL 0x0
#define E1000_OFFSET_EECD 0x10
#define E1000_OFFSET_EERD 0x14

#define E1000_OFFSET_ICR 0xc0
#define E1000_OFFSET_ITR 0xc4
#define E1000_OFFSET_ICS 0xc8
#define E1000_OFFSET_IMS 0xd0
#define E1000_OFFSET_IMC 0xd8

#define E1000_OFFSET_RCTL 0x100
#define E1000_OFFSET_RDBAL 0x2800
#define E1000_OFFSET_RDBAH 0x2804
#define E1000_OFFSET_RDLEN 0x2808
#define E1000_OFFSET_RDH 0x2810
#define E1000_OFFSET_RDT 0x2818

#define E1000_OFFSET_TCTL 0x400
#define E1000_OFFSET_TIPG 0x410
#define E1000_OFFSET_TDBAL 0x3800
#define E1000_OFFSET_TDBAH 0x3804
#define E1000_OFFSET_TDLEN 0x3808
#define E1000_OFFSET_TDH 0x3810
#define E1000_OFFSET_TDT 0x3818

#define E1000_OFFSET_RAL0 0x5400
#define E1000_OFFSET_RAH0 0x5404

#define E1000_INTERRUPT_RXDMT0 BIT(4)
#define E1000_INTERRUPT_RXO BIT(6)
#define E1000_INTERRUPT_RXT0 BIT(7)

#define E1000_TX_DESC_SIZE 16

#define E1000_TX_DESC_STATUS_DD BIT(0)

#define E1000_TX_DESC_CMD_EOP BIT(0)
#define E1000_TX_DESC_CMD_IFCS BIT(1)
#define E1000_TX_DESC_CMD_RS BIT(3)
#define E1000_TX_DESC_CMD_RPS BIT(4)

#define E1000_RX_DESC_SIZE 16

#define E1000_RX_DESC_STATUS_DD BIT(0)
#define E1000_RX_DESC_STATUS_EOP BIT(1)

#define E1000_RCTL_EN BIT(1)
#define E1000_RCTL_UPE BIT(3)
#define E1000_RCTL_MPE BIT(4)
#define E1000_RCTL_BAM BIT(15)

// Maximum ethernet frame size is 1500B so this should work
#define E1000_RX_BUF_SIZE 2048
#define E1000_TX_BUF_SIZE 16288

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

#define E1000_NUM_SUPPORTED_IDS 1

static struct pci_device_id supported_ids[E1000_NUM_SUPPORTED_IDS] = {
    { E1000_VENDOR_ID, E1000_DEVICE_ID },
};

struct __aligned(E1000_TX_DESC_SIZE) e1000_legacy_tx_desc {
    u64 base_addr;
    u16 length;
    u8 cso;
    u8 cmd; // Bit 5 (DEXT) of this field must be 0 to disable the extended format and enable the legacy format.
    u8 status; // Upper four bits are reserved to 0.
    u8 css;
    u16 special;
} __packed;

static_assert(sizeof(struct e1000_legacy_tx_desc) == E1000_TX_DESC_SIZE);
static_assert(alignof(struct e1000_legacy_tx_desc) == E1000_TX_DESC_SIZE);

struct __aligned(E1000_RX_DESC_SIZE) e1000_rx_desc {
    u64 base_addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 error;
    u16 special;
} __packed;

static_assert(sizeof(struct e1000_rx_desc) == E1000_RX_DESC_SIZE);
static_assert(alignof(struct e1000_rx_desc) == E1000_RX_DESC_SIZE);

struct e1000_stats {
    sz n_packets_rx;
    sz n_packets_tx;
    sz n_rxo_interrupts;
    sz n_rxdmt0_interrupts;
    sz n_rxt0_interrupts;
    sz n_interrupts;
};

struct e1000_device {
    struct byte_array tmp_recv_buf; // Just used as a source of memory to receive stuff in the interrupt handler.

    u64 mmio_base;
    u64 mmio_len;

    bool eeprom_normal_access;
    struct mac_addr mac_addr;

    struct e1000_stats stats;

    struct e1000_legacy_tx_desc *tx_queue;
    sz tx_queue_n_desc;
    sz tx_tail;
    byte (*tx_buffers)[E1000_TX_BUF_SIZE];

    struct e1000_rx_desc *rx_queue;
    sz rx_queue_n_desc;
    sz rx_tail;
    byte (*rx_buffers)[E1000_RX_BUF_SIZE];
};

///////////////////////////////////////////////////////////////////////////////
// EEPROM                                                                    //
///////////////////////////////////////////////////////////////////////////////

static void e1000_eeprom_check(struct e1000_device *dev)
{
    // Reference: Section 13.4.4
    //
    // There are two different possible layouts of the EEPROM Read Register (EERD). In the common case, the
    // 'Read Done' field in bit 4. This is not applicable to the 82544GC/EI and 82541xx, however, which use
    // bit 1 as the 'Read Done' bit. But, in both cases, bit 0 is the start bit so we can initiate a read of
    // address 0 and see whether bit 4 is used to signal that the read has completed.
    //
    // I found this algorithm in Serenity OS:
    // https://github.com/SerenityOS/serenity/blob/fc0826cfa9ec57bb0abd8afa135703b59e6050bf/Kernel/Net/Intel/E1000NetworkAdapter.cpp#L280
    mmio_write32(dev->mmio_base + E1000_OFFSET_EERD, BIT(0));
    for (i32 i = 0; i < 999; i++) {
        u32 data = mmio_read32(dev->mmio_base + E1000_OFFSET_EERD);
        if (data & BIT(4)) {
            dev->eeprom_normal_access = true;
            return;
        }
    }
    dev->eeprom_normal_access = false;
}

static u16 e1000_eeprom_read16(struct e1000_device *dev, u8 eeprom_addr)
{
    // Reference: Section 5.3.1, Section 13.4.4.
    assert(dev);

    if (!(mmio_read32(dev->mmio_base + E1000_OFFSET_EECD) & BIT(8)))
        crash("EEPROM not present\n");

    u32 data = 0;
    if (dev->eeprom_normal_access) {
        mmio_write32(dev->mmio_base + E1000_OFFSET_EERD, ((u32)eeprom_addr << 8) | BIT(0));
        while (!((data = mmio_read32(dev->mmio_base + E1000_OFFSET_EERD)) & BIT(4)))
            ;

    } else {
        mmio_write32(dev->mmio_base + E1000_OFFSET_EERD, ((u32)eeprom_addr << 2) | BIT(0));
        while (!((data = mmio_read32(dev->mmio_base + E1000_OFFSET_EERD)) & BIT(1)))
            ;
    }

    u32 tmp = mmio_read32(dev->mmio_base + E1000_OFFSET_EERD);
    tmp &= ~(u32)BIT(0); // Clear the START bit.
    mmio_write32(dev->mmio_base + E1000_OFFSET_EERD, tmp);

    return (data >> 16) & 0xffff;
}

static void e1000_read_mac_addr(struct e1000_device *dev)
{
    // Reference: Table 5-2. Ethernet Controller Address Map
    assert(dev);
    u16 tmp0 = e1000_eeprom_read16(dev, 0);
    u16 tmp1 = e1000_eeprom_read16(dev, 1);
    u16 tmp2 = e1000_eeprom_read16(dev, 2);
    dev->mac_addr = mac_addr_new(tmp0 & 0xff, tmp0 >> 8, tmp1 & 0xff, tmp1 >> 8, tmp2 & 0xff, tmp2 >> 8);
}

///////////////////////////////////////////////////////////////////////////////
// Initialization                                                            //
///////////////////////////////////////////////////////////////////////////////

static struct result e1000_init_mmio(struct e1000_device *dev, enum addr_mapping_memory_type mem_type)
{
    struct addr_mapping mapping;
    mapping.type = ADDR_MAPPING_TYPE_CANONICAL;
    mapping.mem_type = mem_type;
    mapping.perms = PT_FLAG_RW;
    mapping.pbase = dev->mmio_base;
    mapping.vbase = dev->mmio_base;
    mapping.len = dev->mmio_len;
    return paging_map_region(mapping);
}

static void e1000_init_device(struct e1000_device *dev)
{
    assert(dev);

    // Reference: Section 14.3
    u32 ctrl = mmio_read32(dev->mmio_base + E1000_OFFSET_CTRL);
    ctrl &= ~BIT(3); // Clear CTRL.LRST
    ctrl &= ~BIT(7); // Clear CTRL.ILOS
    ctrl &= ~BIT(31); // Clear CTRL.PHY_RST
    mmio_write32(dev->mmio_base + E1000_OFFSET_CTRL, ctrl);
}

static struct result e1000_init_tx(struct e1000_device *dev)
{
    // Reference: Section 14.5

    sz tx_queue_n_desc = 32;

    struct option_byte_array tx_mem_opt =
        kvalloc_alloc(tx_queue_n_desc * sizeof(struct e1000_legacy_tx_desc), alignof(struct e1000_legacy_tx_desc));
    if (tx_mem_opt.is_none)
        return result_error(ENOMEM);
    struct byte_array tx_mem = option_byte_array_checked(tx_mem_opt);
    struct e1000_legacy_tx_desc *tx_queue = byte_array_ptr(tx_mem);

    struct option_byte_array tx_buffers_mem_opt = kvalloc_alloc(tx_queue_n_desc * E1000_TX_BUF_SIZE, 64);
    if (tx_buffers_mem_opt.is_none) {
        kvalloc_free(tx_mem);
        return result_error(ENOMEM);
    }
    struct byte_array tx_buffers_mem = option_byte_array_checked(tx_buffers_mem_opt);

    byte_array_set(tx_mem, 0);
    byte_array_set(tx_buffers_mem, 0);

    // The 8254x needs a physical address because it will use it for DMA.
    struct result_paddr_t paddr_tx_queue_res = virt_to_phys((vaddr_t)tx_queue);
    struct result_paddr_t paddr_tx_buffers_res = virt_to_phys((vaddr_t)byte_array_ptr(tx_buffers_mem));
    if (paddr_tx_queue_res.is_error) {
        kvalloc_free(tx_mem);
        kvalloc_free(tx_buffers_mem);
        return result_error(paddr_tx_queue_res.code);
    }
    if (paddr_tx_buffers_res.is_error) {
        kvalloc_free(tx_mem);
        kvalloc_free(tx_buffers_mem);
        return result_error(paddr_tx_buffers_res.code);
    }
    paddr_t paddr_tx_queue = result_paddr_t_checked(paddr_tx_queue_res);
    paddr_t paddr_tx_buffers = result_paddr_t_checked(paddr_tx_buffers_res);

    // The addresses of these buffers don't change so we can set them once and leave them. The DD bit must be set so
    // the transmit function knows that the descriptors can all be used.
    for (sz i = 0; i < tx_queue_n_desc; i++) {
        tx_queue[i].status |= E1000_TX_DESC_STATUS_DD;
        tx_queue[i].base_addr = paddr_tx_buffers + i * E1000_TX_BUF_SIZE;
    }

    assert(IS_ALIGNED(paddr_tx_queue, 16));
    mmio_write32(dev->mmio_base + E1000_OFFSET_TDBAL, (u64)paddr_tx_queue & 0xffffffff);
    mmio_write32(dev->mmio_base + E1000_OFFSET_TDBAH, ((u64)paddr_tx_queue >> 32) & 0xffffffff);

    assert(IS_ALIGNED(tx_mem.len, 128));
    assert(tx_mem.len <= U32_MAX);
    mmio_write32(dev->mmio_base + E1000_OFFSET_TDLEN, tx_mem.len);

    mmio_write64(dev->mmio_base + E1000_OFFSET_TDH, 0);
    mmio_write64(dev->mmio_base + E1000_OFFSET_TDT, 0);

    u32 tctl = mmio_read32(dev->mmio_base + E1000_OFFSET_TCTL);
    // Set bits Transmit Enable (1) and Pad Short Packets (3). Set Collision Threshold (11:4) to the recommended value
    // of 0xf. Set Collision Distance (21:12) to the recommended value of 0x40 for for full-duplex operation.
    tctl |= BIT(1) | BIT(3) | (0xf << 4) | (0x40 << 12);
    mmio_write32(dev->mmio_base + E1000_OFFSET_TCTL, tctl);

    // These are the recommended values for the IPGT, IPGR1 and IPGR2 fields in the TIPG register for IEEE802.3.
    // Refer to table 13-77.
    mmio_write32(dev->mmio_base + E1000_OFFSET_TIPG, 10 | (8 << 10) | (6 << 20));

    dev->tx_queue = tx_queue;
    dev->tx_buffers = byte_array_ptr(tx_buffers_mem);
    dev->tx_queue_n_desc = tx_queue_n_desc;
    dev->tx_tail = 0;

    return result_ok();
}

static struct result e1000_init_rx(struct e1000_device *dev)
{
    // Reference: Section 14.4

    sz rx_queue_n_desc = 128;

    struct option_byte_array rx_mem_opt =
        kvalloc_alloc(rx_queue_n_desc * sizeof(struct e1000_rx_desc), alignof(struct e1000_rx_desc));
    if (rx_mem_opt.is_none)
        return result_error(ENOMEM);
    struct byte_array rx_mem = option_byte_array_checked(rx_mem_opt);
    struct e1000_rx_desc *rx_queue = byte_array_ptr(rx_mem);

    struct option_byte_array rx_buffers_mem_opt = kvalloc_alloc(rx_queue_n_desc * E1000_RX_BUF_SIZE, 64);
    if (rx_buffers_mem_opt.is_none) {
        kvalloc_free(rx_mem);
        return result_error(ENOMEM);
    }
    struct byte_array rx_buffers_mem = option_byte_array_checked(rx_buffers_mem_opt);

    byte_array_set(rx_mem, 0);
    byte_array_set(rx_buffers_mem, 0);

    // The 8254x needs a physical addresses because it will use them for DMA.
    struct result_paddr_t paddr_rx_queue_res = virt_to_phys((vaddr_t)rx_queue);
    struct result_paddr_t paddr_rx_buffers_res = virt_to_phys((vaddr_t)byte_array_ptr(rx_buffers_mem));
    if (paddr_rx_queue_res.is_error) {
        kvalloc_free(rx_mem);
        kvalloc_free(rx_buffers_mem);
        return result_error(paddr_rx_queue_res.code);
    }
    if (paddr_rx_buffers_res.is_error) {
        kvalloc_free(rx_mem);
        kvalloc_free(rx_buffers_mem);
        return result_error(paddr_rx_buffers_res.code);
    }
    paddr_t paddr_rx_queue = result_paddr_t_checked(paddr_rx_queue_res);
    paddr_t paddr_rx_buffers = result_paddr_t_checked(paddr_rx_buffers_res);

    // Initialize all descriptors to point to the right buffers.
    for (sz i = 0; i < rx_queue_n_desc; i++)
        rx_queue[i].base_addr = paddr_rx_buffers + i * E1000_RX_BUF_SIZE;

    assert(IS_ALIGNED(paddr_rx_queue, 16));
    mmio_write32(dev->mmio_base + E1000_OFFSET_RDBAL, (u64)paddr_rx_queue & 0xffffffff);
    mmio_write32(dev->mmio_base + E1000_OFFSET_RDBAH, ((u64)paddr_rx_queue >> 32) & 0xffffffff);

    assert(IS_ALIGNED(rx_mem.len, 128));
    assert(rx_mem.len <= U32_MAX);
    mmio_write32(dev->mmio_base + E1000_OFFSET_RDLEN, rx_mem.len);

    // This indicates that all descriptors are available for the hardware to store packets.
    mmio_write64(dev->mmio_base + E1000_OFFSET_RDH, 1);
    mmio_write64(dev->mmio_base + E1000_OFFSET_RDT, 0);

    // Set RAL0/RAH0 to the MAC address of the controller so that it accepts packets addressed to it.
    // NOTE: Don't need to set up the Multicast Array Table (MTA) as only one RAL/RAH entry is used.
    mmio_write32(dev->mmio_base + E1000_OFFSET_RAL0, (dev->mac_addr.addr[3] << 24) | (dev->mac_addr.addr[2] << 16) |
                                                         (dev->mac_addr.addr[1] << 8) | dev->mac_addr.addr[0]);
    mmio_write32(dev->mmio_base + E1000_OFFSET_RAH0, BIT(31) | (dev->mac_addr.addr[5] << 8) | dev->mac_addr.addr[4]);

    // TODO: We don't strip the CRC here (bit 26 is the SECRC flag to strip CRCs). I'm not sure, though, if we should.
    u32 rctl = mmio_read32(dev->mmio_base + E1000_OFFSET_RCTL);
    // NOTE: The buffer size is kept at the default of 2048B. Long packet reception and loopback mode are disabled
    // by default. And the Receive Descriptor Minimum Threshold Size (RDMTS) is kept at the default of 1/2 of RDLEN.
    rctl |= E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BAM;
    mmio_write32(dev->mmio_base + E1000_OFFSET_RCTL, rctl);

    dev->rx_queue = rx_queue;
    dev->rx_queue_n_desc = rx_queue_n_desc;
    dev->rx_buffers = byte_array_ptr(rx_buffers_mem);
    dev->rx_tail = 0;

    return result_ok();
}

static void e1000_init_interrupts(struct e1000_device *dev)
{
    pic_enable_irq(11);
    mmio_write32(dev->mmio_base + E1000_OFFSET_IMS,
                 E1000_INTERRUPT_RXDMT0 | E1000_INTERRUPT_RXO | E1000_INTERRUPT_RXT0);
    mmio_write32(dev->mmio_base + E1000_OFFSET_ITR, 500); // Generate interrupts at a maximum rate of 128 mu/s.
    mmio_read32(dev->mmio_base + E1000_OFFSET_ICR);
}

static void e1000_set_link_up(struct e1000_device *dev)
{
    u32 ctrl = mmio_read32(dev->mmio_base + E1000_OFFSET_CTRL);
    ctrl |= BIT(6); // CTRL.SLU
    mmio_write32(dev->mmio_base + E1000_OFFSET_CTRL, ctrl);
}

///////////////////////////////////////////////////////////////////////////////
// Receive and transmit                                                      //
///////////////////////////////////////////////////////////////////////////////

static struct result e1000_tx_poll(struct e1000_device *dev, struct send_buf sb)
{
    struct e1000_legacy_tx_desc *tx_desc = &dev->tx_queue[dev->tx_tail];

    sz len = send_buf_total_length(sb);

    if (len > E1000_TX_BUF_SIZE)
        return result_error(EINVAL);

    // If there is space in the queue, the tail points to a descriptor with the DD flag set, because the 8254x
    // is done with processing this descriptor. Otherwise, the queue is full and we have to wait.
    if (!(tx_desc->status & E1000_TX_DESC_STATUS_DD))
        return result_error(ENOBUFS);

    struct byte_buf tx_buf = byte_buf_new(&dev->tx_buffers[dev->tx_tail], 0, E1000_TX_BUF_SIZE);
    assert(!send_buf_assemble(sb, &tx_buf).is_error);

    tx_desc->length = (u16)len;
    tx_desc->status = 0;
    tx_desc->cmd |= E1000_TX_DESC_CMD_EOP | E1000_TX_DESC_CMD_RS;

    // Advance the tail.
    dev->tx_tail = (dev->tx_tail + 1) % dev->tx_queue_n_desc;
    assert(dev->tx_tail <= U16_MAX);
    mmio_write32(dev->mmio_base + E1000_OFFSET_TDT, dev->tx_tail);

    dev->stats.n_packets_tx++;

    return result_ok();
}

static struct result e1000_rx_poll(struct e1000_device *dev, struct byte_buf *buf)
{
    assert(buf);

    if (buf->cap - buf->len < E1000_RX_BUF_SIZE)
        return result_error(EINVAL);

    // Check the first packet beyond the tail. We need to do it this way because we can't initialize the head
    // and the tail pointer to the same value.
    sz next_tail = (dev->rx_tail + 1) % dev->rx_queue_n_desc;
    assert(next_tail <= U16_MAX);

    struct e1000_rx_desc *rx_desc = &dev->rx_queue[next_tail];

    // The tail points to the first descriptor that has not been processed by software yet. There is nothing for
    // us to do if the hardware hasn't set the Descriptor Done (DD) bit on this descriptor.
    if (!(rx_desc->status & E1000_RX_DESC_STATUS_DD))
        return result_error(EAGAIN);

    // Large packets are disabled and the buffer size should be big enough so that the entire packet could
    // be stored in the buffer. So the End Of Packet (EOP) bit should always be set.
    assert(rx_desc->status & E1000_RX_DESC_STATUS_EOP);

    // The `error` field is only valid when the DD and EOP bits are set.
    if (rx_desc->error)
        return result_error(EIO);

    assert(rx_desc->length <= E1000_RX_BUF_SIZE);
    struct byte_array rx_buf = byte_array_new(&dev->rx_buffers[next_tail], rx_desc->length);

    byte_buf_append(buf, byte_view_from_array(rx_buf));
    byte_array_set(rx_buf, 0);

    // Notice that `rx_desc->base_addr` remains unchanged because the same buffer can now be reused.
    rx_desc->length = 0;
    rx_desc->status = 0;

    // Advance the tail now that the packet beyond the tail as been received.
    dev->rx_tail = next_tail;
    mmio_write32(dev->mmio_base + E1000_OFFSET_RDT, next_tail);

    dev->stats.n_packets_rx++;

    return result_ok();
}

static void e1000_handle_interrupt(struct trap_frame *cpu_state __unused, void *private_data)
{
    assert(private_data);
    struct netdev *netdev = private_data;

    assert(netdev->private_data);
    struct e1000_device *dev = netdev->private_data;

    u32 cause = mmio_read32(dev->mmio_base + E1000_OFFSET_ICR); // This also clears the register to ack' the interrupt.

    dev->stats.n_interrupts++;
    dev->stats.n_rxo_interrupts += cause & E1000_INTERRUPT_RXO ? 1 : 0;
    dev->stats.n_rxdmt0_interrupts += cause & E1000_INTERRUPT_RXDMT0 ? 1 : 0;
    dev->stats.n_rxt0_interrupts += cause & E1000_INTERRUPT_RXT0 ? 1 : 0;

    if (cause & E1000_INTERRUPT_RXO)
        crash("Interrupt receive queue overrun\n");

    if (cause & E1000_INTERRUPT_RXDMT0 || cause & E1000_INTERRUPT_RXT0) {
        while (1) {
            struct byte_buf buf = byte_buf_from_array(dev->tmp_recv_buf);
            struct result res = e1000_rx_poll(dev, &buf);
            if (res.is_error && res.code == EAGAIN)
                break; // Stop trying to receive any more data.
            if (res.is_error)
                crash("Failed to receive\n");
            netdev_intr_receive(netdev, byte_view_from_buf(buf));
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// Outside interface                                                         //
///////////////////////////////////////////////////////////////////////////////

static struct result e1000_netdev_send_frame(struct netdev *netdev, struct send_buf sb)
{
    assert(netdev);
    assert(netdev->private_data);
    struct e1000_device *dev = netdev->private_data;
    assert(mac_addr_is_equal(netdev->mac_addr, dev->mac_addr));
    return e1000_tx_poll(dev, sb);
}

static struct result e1000_probe(struct pci_device *pci)
{
    assert(pci);

    struct option_byte_array dev_mem = kvalloc_alloc(sizeof(struct e1000_device), alignof(struct e1000_device));
    if (dev_mem.is_none)
        return result_error(ENOMEM);
    struct e1000_device *dev = byte_array_ptr(option_byte_array_checked(dev_mem));

    struct option_byte_array netdev_mem = kvalloc_alloc(sizeof(struct netdev), alignof(struct netdev));
    if (netdev_mem.is_none)
        return result_error(ENOMEM);
    struct netdev *netdev = byte_array_ptr(option_byte_array_checked(netdev_mem));

    // This shouldn't last forever:
    dev->tmp_recv_buf = option_byte_array_checked(kvalloc_alloc(E1000_RX_BUF_SIZE, 64));

    dev->mmio_base = pci->bars[0].base;
    dev->mmio_len = pci->bars[0].len;

    byte_array_set(byte_array_new(&dev->stats, sizeof(dev->stats)), 0);

    struct result res = e1000_init_mmio(dev, (pci->bars[0].flags & PCI_BAR_FLAG_PREFETCHABLE) ?
                                                 ADDR_MAPPING_MEMORY_DEFAULT :
                                                 ADDR_MAPPING_MEMORY_STRONG_UNCACHEABLE);
    if (res.is_error)
        return res;

    e1000_eeprom_check(dev);
    e1000_read_mac_addr(dev);

    static byte mac_addr_fmt_buf[MAC_ADDR_FMT_BUF_SIZE];
    struct arena mac_addr_fmt_arn = arena_new(byte_array_new(mac_addr_fmt_buf, countof(mac_addr_fmt_buf)));
    print_dbg(PDBG, STR("EEPROM access mechanism: %s\n"), dev->eeprom_normal_access ? STR("Normal") : STR("Alternate"));
    print_dbg(PINFO, STR("MAC: %s\n"), mac_addr_format(dev->mac_addr, &mac_addr_fmt_arn));

    e1000_init_device(dev);

    res = e1000_init_tx(dev);
    if (res.is_error)
        return res;
    res = e1000_init_rx(dev);
    if (res.is_error)
        return res;

    netdev->mac_addr = dev->mac_addr;
    netdev->ip_addr = ipv4_addr_new(0, 0, 0, 0);
    netdev->link_type = NETDEV_LINK_TYPE_ETHERNET;
    netdev->send_frame = e1000_netdev_send_frame;
    netdev->mtu = E1000_TX_BUF_SIZE;
    netdev->private_data = dev;

    res = netdev_register_device(netdev);
    if (res.is_error)
        return res;

    // NOTE: We DON'T register the `dev` (struct e1000_device) structure with the ISR handler. Instead, we register
    // the `netdev` structure that's also registered with the `netdev` subsystem so that, e.g., we can later retrieve
    // information about the IP address that was assigned to the network device.
    res = isr_register_handler(IRQ_VECTORS_BEG + pci->interrupt_line, e1000_handle_interrupt, netdev);
    if (res.is_error)
        return res;

    e1000_init_interrupts(dev);
    pic_enable_irq(pci->interrupt_line);

    e1000_set_link_up(dev);
    print_dbg(PINFO, STR("Link is up!\n"));

    return result_ok();
}

PCI_REGISTER_DRIVER(e1000, E1000_NUM_SUPPORTED_IDS, supported_ids,
                    PCI_DEVICE_DRIVER_CAP_DMA | PCI_DEVICE_DRIVER_CAP_MEM | PCI_DEVICE_DRIVER_CAP_INTERRUPT,
                    e1000_probe);
