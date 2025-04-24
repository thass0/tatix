// Driver for Intel 82540EM (called e1000 in Linux and QEMU).

#include <tx/asm.h>
#include <tx/base.h>
#include <tx/bytes.h>
#include <tx/kvalloc.h>
#include <tx/paging.h>
#include <tx/pci.h>
#include <tx/print.h>

// The 8254x PCI/PCI-X Family of Gigabit Ethernet Controllers Software Developer’s Manual (2009 version) was used as a
// source for this driver References to sections are with respect to this document. A copy of the manual used can be
// found in the manuals/ directory. It could also be found here at the time of writing:
// https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf

// Reference: Table 13-2. Ethernet Controller Register Summary.
#define E1000_OFFSET_CTRL 0x0
#define E1000_OFFSET_EECD 0x10
#define E1000_OFFSET_EERD 0x14
#define E1000_OFFSET_TCTL 0x400
#define E1000_OFFSET_TIPG 0x410
#define E1000_OFFSET_TDBAL 0x3800
#define E1000_OFFSET_TDBAH 0x3804
#define E1000_OFFSET_TDLEN 0x3808
#define E1000_OFFSET_TDH 0x3810
#define E1000_OFFSET_TDT 0x3818

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

#define E1000_NUM_SUPPORTED_IDS 1

static struct pci_device_id supported_ids[E1000_NUM_SUPPORTED_IDS] = {
    { E1000_VENDOR_ID, E1000_DEVICE_ID },
};

struct e1000_device {
    u64 mmio_base;
    u64 mmio_len;
    bool eeprom_normal_access;
    u8 mac_addr[6];
    struct addr_mapping tx_queue;
    sz tx_tail;
};

struct __aligned(16) e1000_legacy_tx_desc {
    u64 base_addr;
    u16 length;
    u8 cso;
    u8 cmd; // Bit 5 (DEXT) of this field must be 0 to disable the extended format.
    u8 status; // Upper four bits are reserved to 0.
    u8 css;
    u16 special;
} __packed;

static_assert(sizeof(struct e1000_legacy_tx_desc) == 16);

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
    u16 tmp = e1000_eeprom_read16(dev, 0);
    dev->mac_addr[0] = tmp & 0xff;
    dev->mac_addr[1] = tmp >> 8;
    tmp = e1000_eeprom_read16(dev, 1);
    dev->mac_addr[2] = tmp & 0xff;
    dev->mac_addr[3] = tmp >> 8;
    tmp = e1000_eeprom_read16(dev, 2);
    dev->mac_addr[4] = tmp & 0xff;
    dev->mac_addr[5] = tmp >> 8;
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

    assert(alignof(struct e1000_legacy_tx_desc) == 16);
    sz tx_mem_size = 32 * sizeof(struct e1000_legacy_tx_desc);
    ptr tx_mem = dev->mmio_base + dev->mmio_len;

    // The TX queue is identity mapped in memory right after the identity mapping for the 8254x MMIO region.
    struct addr_mapping tx_queue;
    tx_queue.type = ADDR_MAPPING_TYPE_CANONICAL;
    tx_queue.mem_type = ADDR_MAPPING_MEMORY_STRONG_UNCACHEABLE;
    tx_queue.perms = PT_FLAG_RW;
    tx_queue.vbase = dev->mmio_base + dev->mmio_len;
    tx_queue.pbase = tx_queue.vbase;
    tx_queue.len = tx_mem_size;

    struct result res = paging_map_region(tx_queue);
    if (res.is_error)
        return res;

    memset(bytes_new((void *)tx_mem, tx_mem_size), 0);

    dev->tx_queue = tx_queue;
    dev->tx_tail = 0;

    assert(IS_ALIGNED((ptr)tx_mem, 16));
    assert(IS_ALIGNED(tx_mem_size, 128));
    assert(tx_mem_size <= U32_MAX);

    mmio_write32(dev->mmio_base + E1000_OFFSET_TDBAL, (u64)tx_mem & 0xffffffff);
    mmio_write32(dev->mmio_base + E1000_OFFSET_TDBAH, ((u64)tx_mem >> 32) & 0xffffffff);
    mmio_write32(dev->mmio_base + E1000_OFFSET_TDLEN, tx_mem_size);

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

    return result_ok();
}

static struct result e1000_probe(struct pci_device *pci)
{
    assert(pci);

    print_dbg(STR("Hello, I'm the e1000 driver!\n"));

    struct e1000_device *dev = kvalloc_alloc(sizeof(struct e1000_device), alignof(struct e1000_device));
    if (!dev)
        return result_error(ENOMEM);

    dev->mmio_base = pci->bars[0].base;
    dev->mmio_len = pci->bars[0].len;

    struct result res = e1000_init_mmio(dev, (pci->bars[0].flags & PCI_BAR_FLAG_PREFETCHABLE) ?
                                                 ADDR_MAPPING_MEMORY_DEFAULT :
                                                 ADDR_MAPPING_MEMORY_STRONG_UNCACHEABLE);
    assert(!res.is_error);

    e1000_eeprom_check(dev);
    e1000_read_mac_addr(dev);

    print_dbg(STR("EEPROM access mechanism: %s\n"), dev->eeprom_normal_access ? STR("Normal") : STR("Alternate"));
    print_dbg(STR("MAC: %hhx:%hhx:%hhx:%hhx:%hhx:%hhx\n"), dev->mac_addr[0], dev->mac_addr[1], dev->mac_addr[2],
              dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5]);

    e1000_init_device(dev);

    res = e1000_init_tx(dev);
    assert(!res.is_error);

    return result_ok();
}

PCI_REGISTER_DRIVER(e1000, E1000_NUM_SUPPORTED_IDS, supported_ids, e1000_probe);
