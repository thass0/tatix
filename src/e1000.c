// Driver for Intel 82540EM (called e1000 in Linux and QEMU).

#include <tx/asm.h>
#include <tx/base.h>
#include <tx/bytes.h>
#include <tx/e1000.h>
#include <tx/print.h>

#define PCI_PORT_CONFIG_ADDRESS (u16)0xcf8
#define PCI_PORT_CONFIG_DATA (u16)0xcfc

// NOTE: The configuration space can only be accessed with a four-byte granularity.
// But these offsets (finer grained) offsets can be used with `pci_config_readx()`.
#define PCI_OFFSET_VENDOR_ID 0x00
#define PCI_OFFSET_DEVICE_ID 0x02
#define PCI_OFFSET_COMMAND 0x04
#define PCI_OFFSET_STATUS 0x06
#define PCI_OFFSET_REVISION_ID 0x08
#define PCI_OFFSET_PROG_IF 0x09
#define PCI_OFFSET_SUBCLASS 0x0a
#define PCI_OFFSET_CLASS_CODE 0x0b
#define PCI_OFFSET_CACHE_LINE_SIZE 0x0c
#define PCI_OFFSET_LATENCY_TIMER 0x0d
#define PCI_OFFSET_HEADER_TYPE 0x0e
#define PCI_OFFSET_BIST 0x0f

#define PCI_OFFSET_HDR0_BAR0 0x10
#define PCI_OFFSET_HDR0_BAR1 0x14
#define PCI_OFFSET_HDR0_BAR2 0x18
#define PCI_OFFSET_HDR0_BAR3 0x1c
#define PCI_OFFSET_HDR0_BAR4 0x20
#define PCI_OFFSET_HDR0_BAR5 0x24
#define PCI_OFFSET_HDR0_CARDBUS_CIS_PTR 0x28
#define PCI_OFFSET_HDR0_SUBSYSTEM_VENDOR_ID 0x2c
#define PCI_OFFSET_HDR0_SUBSYSTEM_ID 0x2e
#define PCI_OFFSET_HDR0_EXPANSION_ROM_BASE_ADDR 0x30
#define PCI_OFFSET_HDR0_CAPABILITIES_PTR 0x34
#define PCI_OFFSET_HDR0_INTERRUPT_LINE 0x3c
#define PCI_OFFSET_HDR0_INTERRUPT_PIN 0x3d
#define PCI_OFFSET_HDR0_MIN_GRANT 0x3e
#define PCI_OFFSET_HDR0_MAX_LATENCY 0x3f

#define PCI_MASK_HEADER_TYPE (~BIT(7))

// These limits are defined by the PCI specification.
#define PCI_NUM_FUNCTIONS 8
#define PCI_MAX_DEVICES 32
#define PCI_MAX_BUSSES 256

struct pci_header_common {
    u16 vendor_id;
    u16 device_id;
    u16 command;
    u16 status;
    u8 revision_id;
    u8 prog_if;
    u8 subclass;
    u8 class_code;
    u8 cache_line_size;
    u8 latency_timer;
    u8 header_type; // Bit 7 indicates whether this device has multiple functions.
    u8 bist;
} __packed;

struct pci_header_type_0 {
    u32 bar0;
    u32 bar1;
    u32 bar2;
    u32 bar3;
    u32 bar4;
    u32 bar5;
    u32 cardbus_cis_ptr;
    u16 subsystem_vendor_id;
    u16 subsystem_id;
    u32 expansion_rom_base_addr;
    u8 capabilities_ptr;
    u8 reserved[7];
    u8 interrupt_line;
    u8 interrupt_pin;
    u8 min_grant;
    u8 max_latency;
} __packed;

static_assert(sizeof(struct pci_header_type_0) == 64 - sizeof(struct pci_header_common));

u32 pci_config_read32(u8 bus, u8 slot, u8 func, u8 offset)
{
    assert(IS_ALIGNED(offset, 4));
    u32 address = (u32)bus << 16 | (u32)slot << 11 | (u32)func << 8 | (u32)offset | (u32)BIT(31);
    outl(PCI_PORT_CONFIG_ADDRESS, address);
    return inl(PCI_PORT_CONFIG_DATA);
}

u16 pci_config_read16(u8 bus, u8 slot, u8 func, u8 offset)
{
    assert(IS_ALIGNED(offset, 2));
    u32 ret = pci_config_read32(bus, slot, func, offset & ~3);
    return ret >> ((offset & 2) * 8) & 0xffff;
}

u8 pci_config_read8(u8 bus, u8 slot, u8 func, u8 offset)
{
    u16 ret = pci_config_read16(bus, slot, func, offset & ~1);
    return ret >> ((offset & 1) * 8) & 0xff;
}

bool pci_device_exists(u8 bus, u8 slot, u8 func)
{
    // The vendor ID 0xffff has intentionally not been assigned to any vendor so that it can be used
    // to check if a device exists.
    u16 vendor_id = pci_config_read16(bus, slot, func, PCI_OFFSET_VENDOR_ID);
    return vendor_id != 0xffff;
}

bool pci_check_device(u8 bus, u8 slot)
{
    if (!pci_device_exists(bus, slot, 0))
        return false;

    u8 header_type = pci_config_read8(bus, slot, 0, PCI_OFFSET_HEADER_TYPE);
    if (header_type & BIT(7)) {
        // It's a multi-function device.
        for (u8 func = 1; func < PCI_NUM_FUNCTIONS; func++)
            if (!pci_device_exists(bus, slot, func))
                return false;
    }

    return true;
}

struct pci_header_common pci_load_header_common(u8 bus, u8 slot, u8 func)
{
    struct pci_header_common common;
    common.vendor_id = pci_config_read16(bus, slot, func, PCI_OFFSET_VENDOR_ID);
    common.device_id = pci_config_read16(bus, slot, func, PCI_OFFSET_DEVICE_ID);
    common.command = pci_config_read16(bus, slot, func, PCI_OFFSET_COMMAND);
    common.status = pci_config_read16(bus, slot, func, PCI_OFFSET_STATUS);
    common.revision_id = pci_config_read8(bus, slot, func, PCI_OFFSET_REVISION_ID);
    common.prog_if = pci_config_read8(bus, slot, func, PCI_OFFSET_PROG_IF);
    common.subclass = pci_config_read8(bus, slot, func, PCI_OFFSET_SUBCLASS);
    common.class_code = pci_config_read8(bus, slot, func, PCI_OFFSET_CLASS_CODE);
    common.cache_line_size = pci_config_read8(bus, slot, func, PCI_OFFSET_CACHE_LINE_SIZE);
    common.latency_timer = pci_config_read8(bus, slot, func, PCI_OFFSET_LATENCY_TIMER);
    common.header_type = pci_config_read8(bus, slot, func, PCI_OFFSET_HEADER_TYPE);
    common.bist = pci_config_read8(bus, slot, func, PCI_OFFSET_BIST);
    return common;
}

struct pci_header_type_0 pci_load_header_type_0(u8 bus, u8 slot, u8 func)
{
    struct pci_header_type_0 hdr;
    memset(bytes_new(&hdr.reserved, sizeof(hdr.reserved)), 0);
    hdr.bar0 = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_BAR0);
    hdr.bar1 = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_BAR1);
    hdr.bar2 = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_BAR2);
    hdr.bar3 = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_BAR3);
    hdr.bar4 = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_BAR4);
    hdr.bar5 = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_BAR5);
    hdr.cardbus_cis_ptr = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_CARDBUS_CIS_PTR);
    hdr.subsystem_vendor_id = pci_config_read16(bus, slot, func, PCI_OFFSET_HDR0_SUBSYSTEM_VENDOR_ID);
    hdr.subsystem_id = pci_config_read16(bus, slot, func, PCI_OFFSET_HDR0_SUBSYSTEM_ID);
    hdr.expansion_rom_base_addr = pci_config_read32(bus, slot, func, PCI_OFFSET_HDR0_EXPANSION_ROM_BASE_ADDR);
    hdr.capabilities_ptr = pci_config_read8(bus, slot, func, PCI_OFFSET_HDR0_CAPABILITIES_PTR);
    hdr.interrupt_line = pci_config_read8(bus, slot, func, PCI_OFFSET_HDR0_INTERRUPT_LINE);
    hdr.interrupt_pin = pci_config_read8(bus, slot, func, PCI_OFFSET_HDR0_INTERRUPT_PIN);
    hdr.min_grant = pci_config_read8(bus, slot, func, PCI_OFFSET_HDR0_MIN_GRANT);
    hdr.max_latency = pci_config_read8(bus, slot, func, PCI_OFFSET_HDR0_MAX_LATENCY);
    return hdr;
}

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

struct result e1000_init(void)
{
    struct pci_header_common common;
    struct pci_header_type_0 hdr;
    // NOTE: `u16` is used for `bus` so that we can compare it to `PCI_MAX_BUSSES` (which is 256).
    for (u16 bus = 0; bus < PCI_MAX_BUSSES; bus++) {
        for (u8 slot = 0; slot < PCI_MAX_DEVICES; slot++) {
            if (pci_check_device(bus, slot)) {
                common = pci_load_header_common(bus, slot, 0);
                print_dbg(STR("PCI Device: %hx:%hx (%s)\n"), common.vendor_id, common.device_id,
                          common.vendor_id == E1000_VENDOR_ID && common.device_id == E1000_DEVICE_ID ?
                              STR("Intel 82540EM") :
                              STR("Unknown"));
                if ((common.header_type & PCI_MASK_HEADER_TYPE) == 0) {
                    hdr = pci_load_header_type_0(bus, slot, 0);
                    print_dbg(STR("BAR0: 0x%lx\n"), hdr.bar0);
                    print_dbg(STR("BAR1: 0x%lx\n"), hdr.bar1);
                    print_dbg(STR("Interrupt line: %u\n"), (u32)hdr.interrupt_line);
                }
            }
        }
    }
    return result_ok();
}
