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

// For reference, this is what the common and type 0 PCI headers look like:
//
// struct pci_header_common {
//     u16 vendor_id;
//     u16 device_id;
//     u16 command;
//     u16 status;
//     u8 revision_id;
//     u8 prog_if;
//     u8 subclass;
//     u8 class_code;
//     u8 cache_line_size;
//     u8 latency_timer;
//     u8 header_type; // Bit 7 indicates whether this device has multiple functions.
//     u8 bist;
// } __packed;
//
// struct pci_header_type_0 {
//     u32 bar0;
//     u32 bar1;
//     u32 bar2;
//     u32 bar3;
//     u32 bar4;
//     u32 bar5;
//     u32 cardbus_cis_ptr;
//     u16 subsystem_vendor_id;
//     u16 subsystem_id;
//     u32 expansion_rom_base_addr;
//     u8 capabilities_ptr;
//     u8 reserved[7];
//     u8 interrupt_line;
//     u8 interrupt_pin;
//     u8 min_grant;
//     u8 max_latency;
// } __packed;

#define PCI_MASK_HEADER_TYPE (~BIT(7))

// Fields in the command register
#define PCI_REGISTER_COMMAND_IO_SPACE BIT(0)
#define PCI_REGISTER_COMMAND_MEM_SPACE BIT(1)

// These limits are defined by the PCI specification.
#define PCI_NUM_FUNCTIONS 8
#define PCI_MAX_DEVICES 32
#define PCI_MAX_BUSSES 256

#define PCI_MASK_BAR_TYPE BIT(0)
#define PCI_MASK_BAR_MEM_TYPE (BIT(1) | BIT(2))
#define PCI_MASK_BAR_MEM_PREFETCHABLE BIT(3)
#define PCI_MASK_BAR_MEM_ADDR 0xfffffff0
#define PCI_MASK_BAR_IO_ADDR 0xfffffffc

enum pci_resource_type {
    PCI_RESOURCE_TYPE_MEM64,
    PCI_RESOURCE_TYPE_MEM32,
    PCI_RESOURCE_TYPE_IO,
};

#define PCI_RESOURCE_FLAG_PREFETCHABLE BIT(0)

struct pci_resource {
    bool used;
    enum pci_resource_type type;
    u16 flags;

    u64 base;
    u64 len;
};

#define PCI_MAX_BARS 6

struct pci_device {
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision_id;
    u8 bus;
    u8 device;
    u8 func;

    struct pci_resource bars[PCI_MAX_BARS];
};

struct result_u32 pci_config_read32(u8 bus, u8 device, u8 func, u8 offset)
{
    assert(IS_ALIGNED(offset, 4));
    u32 address = (u32)bus << 16 | (u32)device << 11 | (u32)func << 8 | (u32)offset | (u32)BIT(31);
    outl(PCI_PORT_CONFIG_ADDRESS, address);
    u32 ret = inl(PCI_PORT_CONFIG_DATA);
    if (ret == 0xffffffff)
        return result_u32_error(ENODEV);
    return result_u32_ok(ret);
}

struct result_u16 pci_config_read16(u8 bus, u8 device, u8 func, u8 offset)
{
    assert(IS_ALIGNED(offset, 2));
    struct result_u32 res = pci_config_read32(bus, device, func, offset & ~3);
    if (res.is_error)
        return result_u16_error(res.code);
    u16 ret = result_u32_checked(res) >> ((offset & 2) * 8) & 0xffff;
    if (ret == 0xffff)
        return result_u16_error(ENODEV);
    return result_u16_ok(ret);
}

struct result_u8 pci_config_read8(u8 bus, u8 device, u8 func, u8 offset)
{
    struct result_u16 res = pci_config_read16(bus, device, func, offset & ~1);
    if (res.is_error)
        return result_u8_error(res.code);
    u8 ret = result_u16_checked(res) >> ((offset & 1) * 8) & 0xff;
    if (ret == 0xff)
        return result_u8_error(ENODEV);
    return result_u8_ok(ret);
}

// NOTE: We can't do any error checking on writes. If a write to a non-existent device is made, all data
// is discarded. Checking if a subsequent read returns the same value that was just written also doesn't confirm a
// write as not all registers behave like that (e.g., see how the length of a BAR is retrieved).

void pci_config_write32(u8 bus, u8 device, u8 func, u8 offset, u32 value)
{
    assert(IS_ALIGNED(offset, 4));
    u32 address = (u32)bus << 16 | (u32)device << 11 | (u32)func << 8 | (u32)offset | (u32)BIT(31);
    outl(PCI_PORT_CONFIG_ADDRESS, address);
    outl(PCI_PORT_CONFIG_DATA, value);
}

void pci_config_write16(u8 bus, u8 device, u8 func, u8 offset, u16 value)
{
    // NOTE: We can't delegate the work to `pci_config_write32` here, as we did with `pci_config_readx`,
    // since we would overwrite other registers.
    assert(IS_ALIGNED(offset, 2));
    u32 address = (u32)bus << 16 | (u32)device << 11 | (u32)func << 8 | (u32)offset | (u32)BIT(31);
    outl(PCI_PORT_CONFIG_ADDRESS, address);
    outw(PCI_PORT_CONFIG_DATA, value);
}

void pci_config_write8(u8 bus, u8 device, u8 func, u8 offset, u8 value)
{
    u32 address = (u32)bus << 16 | (u32)device << 11 | (u32)func << 8 | (u32)offset | (u32)BIT(31);
    outl(PCI_PORT_CONFIG_ADDRESS, address);
    outb(PCI_PORT_CONFIG_DATA, value);
}

bool pci_device_exists(u8 bus, u8 device, u8 func)
{
    // The vendor ID 0xffff has intentionally not been assigned to any vendor so that it can be used
    // to check if a device exists (`pci_config_read16` returns an error if the read is all ones).
    struct result_u16 res = pci_config_read16(bus, device, func, PCI_OFFSET_VENDOR_ID);
    return !res.is_error;
}

struct result_u32 pci_get_bar_len(u8 bus, u8 device, u8 func, i32 bar_idx, u32 mask)
{
    struct result_u32 res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR0 + bar_idx * 4);
    if (res.is_error)
        return res;
    u32 orig_bar = result_u32_checked(res);

    // Write all ones to the register.
    pci_config_write32(bus, device, func, PCI_OFFSET_HDR0_BAR0 + bar_idx * 4, 0xffffffff);

    res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR0 + bar_idx * 4);
    if (res.is_error)
        return res;
    u32 len = result_u32_checked(res);

    // These steps are requied to get the actual length from the result of the read.
    len &= mask; // The mask depends on the type of the BAR.
    len = ~len;
    len += 1;

    // Restore the original value.
    pci_config_write32(bus, device, func, PCI_OFFSET_HDR0_BAR0 + bar_idx * 4, orig_bar);

    return result_u32_ok(len);
}

struct result pci_get_resource_info(u8 bus, u8 device, u8 func, struct pci_resource (*bars)[PCI_MAX_BARS])
{
    // We will be accessing all six (`PCI_MAX_BARS`) BARs here. This number of BARs is only used in the
    // type 0 header. So, before accessing other fields as BARs, we need to confirm that this is a type 0 header.
    struct result_u8 header_type_res = pci_config_read8(bus, device, func, PCI_OFFSET_HEADER_TYPE);
    if (header_type_res.is_error)
        return result_error(header_type_res.code);
    assert((result_u8_checked(header_type_res) & PCI_MASK_HEADER_TYPE) == 0);

    // Disable I/O and memory decode while reading/writing from/to BARs. The OSDev Wiki says: "This is needed as some
    // devices are known to decode the write of all ones to the register as an (unintended) access."
    struct result_u16 cmd_res = pci_config_read16(bus, device, func, PCI_OFFSET_COMMAND);
    if (cmd_res.is_error)
        return result_error(cmd_res.code);
    u16 orig_cmd = result_u16_checked(cmd_res);

    u16 tmp_cmd = orig_cmd & ~(PCI_REGISTER_COMMAND_IO_SPACE | PCI_REGISTER_COMMAND_MEM_SPACE);
    pci_config_write16(bus, device, func, PCI_OFFSET_COMMAND, tmp_cmd);

    // Super verbose, but there is no way around this ...
    u32 raw_bars[PCI_MAX_BARS];
    struct result_u32 bar0_res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR0);
    if (bar0_res.is_error)
        return result_error(bar0_res.code);
    raw_bars[0] = result_u32_checked(bar0_res);
    struct result_u32 bar1_res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR1);
    if (bar1_res.is_error)
        return result_error(bar1_res.code);
    raw_bars[1] = result_u32_checked(bar1_res);
    struct result_u32 bar2_res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR2);
    if (bar2_res.is_error)
        return result_error(bar2_res.code);
    raw_bars[2] = result_u32_checked(bar2_res);
    struct result_u32 bar3_res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR3);
    if (bar3_res.is_error)
        return result_error(bar3_res.code);
    raw_bars[3] = result_u32_checked(bar3_res);
    struct result_u32 bar4_res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR4);
    if (bar4_res.is_error)
        return result_error(bar4_res.code);
    raw_bars[4] = result_u32_checked(bar4_res);
    struct result_u32 bar5_res = pci_config_read32(bus, device, func, PCI_OFFSET_HDR0_BAR5);
    if (bar5_res.is_error)
        return result_error(bar5_res.code);
    raw_bars[5] = result_u32_checked(bar5_res);

    memset(bytes_new(bars, sizeof(*bars)), 0);

    for (i32 i = 0; i < PCI_MAX_BARS; i++) {
        if ((raw_bars[i] & PCI_MASK_BAR_TYPE) == 1) {
            (*bars)[i].type = PCI_RESOURCE_TYPE_IO;
            (*bars)[i].base = raw_bars[i] & PCI_MASK_BAR_IO_ADDR;
            (*bars)[i].flags = 0;

            struct result_u32 bar_len_res = pci_get_bar_len(bus, device, func, i, PCI_MASK_BAR_IO_ADDR);
            if (bar_len_res.is_error)
                return result_error(bar_len_res.code);
            (*bars)[i].len = result_u32_checked(bar_len_res);

            (*bars)[i].used = true;
        } else {
            // NOTE: We check memory space BAR types 0 (32 bits) and 2 (64 bits) here. In old PCI versions,
            // type 1 was used for a 16-bit wide base register, but this type has since been reserved.
            if ((raw_bars[i] & PCI_MASK_BAR_MEM_TYPE) == 0) {
                (*bars)[i].type = PCI_RESOURCE_TYPE_MEM32;
                (*bars)[i].base = raw_bars[i] & PCI_MASK_BAR_MEM_ADDR;
                (*bars)[i].flags = (raw_bars[i] & PCI_MASK_BAR_MEM_PREFETCHABLE) ? PCI_RESOURCE_FLAG_PREFETCHABLE : 0;

                struct result_u32 bar_len_res = pci_get_bar_len(bus, device, func, i, PCI_MASK_BAR_MEM_ADDR);
                if (bar_len_res.is_error)
                    return result_error(bar_len_res.code);
                (*bars)[i].len = (u64)result_u32_checked(bar_len_res);

                (*bars)[i].used = true;
            } else if ((raw_bars[i] & PCI_MASK_BAR_MEM_TYPE) == 2) {
                if (i + 1 == PCI_MAX_BARS)
                    return result_error(EINVAL); // This is a 64-bit entry. It requires that there is another BAR.

                (*bars)[i].type = PCI_RESOURCE_TYPE_MEM64;
                (*bars)[i].base = (u64)(raw_bars[i] & PCI_MASK_BAR_MEM_ADDR) + ((u64)raw_bars[i + 1] << 32);
                (*bars)[i].flags = (raw_bars[i] & PCI_MASK_BAR_MEM_PREFETCHABLE) ? PCI_RESOURCE_FLAG_PREFETCHABLE : 0;

                struct result_u32 bar_len_low_res = pci_get_bar_len(bus, device, func, i, PCI_MASK_BAR_MEM_ADDR);
                struct result_u32 bar_len_high_res = pci_get_bar_len(bus, device, func, i + 1, 0xffffffff);
                if (bar_len_low_res.is_error)
                    return result_error(bar_len_low_res.code);
                if (bar_len_high_res.is_error)
                    return result_error(bar_len_high_res.code);
                (*bars)[i].len = (u64)result_u32_checked(bar_len_low_res) |
                                 ((u64)result_u32_checked(bar_len_high_res) << 32);

                (*bars)[i].used = true;
                i += 1; // Do an extra increment to skip the second register of this 64-bit BAR.
            }
        }
    }

    // Restore the original value of the command register.
    pci_config_write16(bus, device, func, PCI_OFFSET_COMMAND, orig_cmd);

    return result_ok();
}

struct result pci_get_device(u16 vendor_id, u16 device_id, struct pci_device *dev)
{
    assert(dev);
    // NOTE: `u16` is used for `bus` so that we can compare it to `PCI_MAX_BUSSES` (which is 256).
    for (u16 bus = 0; bus < PCI_MAX_BUSSES; bus++) {
        for (u8 device = 0; device < PCI_MAX_DEVICES; device++) {
            // NOTE: Only testing function 0 here.
            if (!pci_device_exists(bus, device, 0))
                continue;

            struct result_u16 vendor_id_res = pci_config_read16(bus, device, 0, PCI_OFFSET_VENDOR_ID);
            struct result_u16 device_id_res = pci_config_read16(bus, device, 0, PCI_OFFSET_DEVICE_ID);
            if (vendor_id_res.is_error || device_id_res.is_error)
                return result_error(EIO);

            if (result_u16_checked(vendor_id_res) != vendor_id || result_u16_checked(device_id_res) != device_id)
                continue;

            struct result_u8 header_type_res = pci_config_read8(bus, device, 0, PCI_OFFSET_HEADER_TYPE);
            if (header_type_res.is_error)
                return result_error(header_type_res.code);

            assert((result_u8_checked(header_type_res) & PCI_MASK_HEADER_TYPE) ==
                   0); // TODO: Support other header types when needed.

            struct result_u8 class_code_res = pci_config_read8(bus, device, 0, PCI_OFFSET_CLASS_CODE);
            struct result_u8 subclass_res = pci_config_read8(bus, device, 0, PCI_OFFSET_SUBCLASS);
            struct result_u8 prog_if_res = pci_config_read8(bus, device, 0, PCI_OFFSET_PROG_IF);
            struct result_u8 revision_id_res = pci_config_read8(bus, device, 0, PCI_OFFSET_REVISION_ID);
            if (class_code_res.is_error || subclass_res.is_error || prog_if_res.is_error || revision_id_res.is_error)
                return result_error(EIO);

            dev->vendor_id = result_u16_checked(vendor_id_res);
            dev->device_id = result_u16_checked(device_id_res);
            dev->class_code = result_u8_checked(class_code_res);
            dev->subclass = result_u8_checked(subclass_res);
            dev->prog_if = result_u8_checked(prog_if_res);
            dev->revision_id = result_u8_checked(revision_id_res);

            dev->bus = bus;
            dev->device = device;
            dev->func = 0;

            struct result res = pci_get_resource_info(bus, device, 0, &dev->bars);
            if (res.is_error)
                return result_error(res.code);

            return result_ok();
        }
    }

    return result_error(ENODEV);
}

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

struct result e1000_init(void)
{
    struct pci_device dev;
    struct result res = pci_get_device(E1000_VENDOR_ID, E1000_DEVICE_ID, &dev);
    assert(!res.is_error);
    print_dbg(STR("E1000 device:\n"));
    print_dbg(STR("PCI:%hhx:%hhx.%hhx [%hx:%hx]\n"), dev.bus, dev.device, dev.func, dev.vendor_id, dev.device_id);
    for (i32 i = 0; i < PCI_MAX_BARS; i++) {
        if (dev.bars[i].used) {
            print_dbg(STR("BAR%d: base=0x%lx, len=0x%lx (%s)\n"), i, dev.bars[i].base, dev.bars[i].len,
                      dev.bars[i].type == PCI_RESOURCE_TYPE_IO ? STR("IO") : STR("MEM"));
        }
    }
    return result_ok();
}
