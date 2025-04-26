#include <tx/arena.h>
#include <tx/byte.h>
#include <tx/error.h>
#include <tx/kvalloc.h>
#include <tx/list.h>
#include <tx/pci.h>
#include <tx/print.h>

///////////////////////////////////////////////////////////////////////////////
// Configuration space reading and writing                                   //
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// Driver lookups                                                            //
///////////////////////////////////////////////////////////////////////////////

extern struct pci_device_driver _pci_device_driver_list_start[];
extern struct pci_device_driver _pci_device_driver_list_end[];

struct pci_device_driver *pci_lookup_driver(u16 vendor_id, u16 device_id)
{
    sz n_matches = 0;
    struct pci_device_driver *latest_match = NULL;

    for (struct pci_device_driver *drv = _pci_device_driver_list_start; drv != _pci_device_driver_list_end; drv++) {
        for (sz i = 0; i < drv->n_ids; i++) {
            if (drv->ids[i].vendor == vendor_id && drv->ids[i].device == device_id) {
                n_matches++;
                latest_match = drv;
            }
        }
    }

    if (n_matches == 1) {
        assert(latest_match); // Can't be `NULL` if `n_matches == 1`.
        return latest_match; // The latest match is also the only match since `n_matches == 1`.
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Probing devices                                                           //
///////////////////////////////////////////////////////////////////////////////

static bool pci_device_exists(u8 bus, u8 device, u8 func)
{
    // The vendor ID 0xffff has intentionally not been assigned to any vendor so that it can be used
    // to check if a device exists (`pci_config_read16` returns an error if the read is all ones).
    struct result_u16 res = pci_config_read16(bus, device, func, PCI_OFFSET_VENDOR_ID);
    return !res.is_error;
}

static struct result_u32 pci_get_bar_len(u8 bus, u8 device, u8 func, i32 bar_idx, u32 mask)
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

static struct result pci_get_resource_info(u8 bus, u8 device, u8 func, struct pci_bar (*bars)[PCI_MAX_BARS])
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

    byte_array_set(byte_array_new(bars, sizeof(*bars)), 0);

    for (i32 i = 0; i < PCI_MAX_BARS; i++) {
        if ((raw_bars[i] & PCI_MASK_BAR_TYPE) == 1) {
            (*bars)[i].type = PCI_BAR_TYPE_IO;
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
                (*bars)[i].type = PCI_BAR_TYPE_MEM;
                (*bars)[i].base = raw_bars[i] & PCI_MASK_BAR_MEM_ADDR;
                (*bars)[i].flags = (raw_bars[i] & PCI_MASK_BAR_MEM_PREFETCHABLE) ? PCI_BAR_FLAG_PREFETCHABLE : 0;

                struct result_u32 bar_len_res = pci_get_bar_len(bus, device, func, i, PCI_MASK_BAR_MEM_ADDR);
                if (bar_len_res.is_error)
                    return result_error(bar_len_res.code);
                (*bars)[i].len = (u64)result_u32_checked(bar_len_res);

                (*bars)[i].used = true;
            } else if ((raw_bars[i] & PCI_MASK_BAR_MEM_TYPE) == 2) {
                if (i + 1 == PCI_MAX_BARS)
                    return result_error(EINVAL); // This is a 64-bit entry. It requires that there is another BAR.

                (*bars)[i].type = PCI_BAR_TYPE_MEM;
                (*bars)[i].base = (u64)(raw_bars[i] & PCI_MASK_BAR_MEM_ADDR) + ((u64)raw_bars[i + 1] << 32);
                (*bars)[i].flags = (raw_bars[i] & PCI_MASK_BAR_MEM_PREFETCHABLE) ? PCI_BAR_FLAG_PREFETCHABLE : 0;

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

static struct result pci_set_driver_capabilities(struct pci_device *dev, u16 capabilities)
{
    assert(dev);

    struct result_u16 cmd_res = pci_config_read16(dev->bus, dev->device, dev->func, PCI_OFFSET_COMMAND);
    if (cmd_res.is_error)
        return result_error(cmd_res.code);

    u16 cmd = result_u16_checked(cmd_res);
    if (capabilities & PCI_DEVICE_DRIVER_CAP_IO)
        cmd |= PCI_REGISTER_COMMAND_IO_SPACE;
    else
        cmd &= ~PCI_REGISTER_COMMAND_IO_SPACE;
    if (capabilities & PCI_DEVICE_DRIVER_CAP_MEM)
        cmd |= PCI_REGISTER_COMMAND_MEM_SPACE;
    else
        cmd &= ~PCI_REGISTER_COMMAND_MEM_SPACE;
    if (capabilities & PCI_DEVICE_DRIVER_CAP_DMA)
        cmd |= PCI_REGISTER_COMMAND_BUS_MASTER;
    else
        cmd &= ~PCI_REGISTER_COMMAND_BUS_MASTER;
    if (capabilities & PCI_DEVICE_DRIVER_CAP_INTERRUPT)
        cmd &= ~PCI_REGISTER_COMMAND_INTERRUPT_DISABLE;
    else
        cmd |= PCI_REGISTER_COMMAND_INTERRUPT_DISABLE;

    pci_config_write16(dev->bus, dev->device, dev->func, PCI_OFFSET_COMMAND, cmd);

    return result_ok();
}

struct result pci_probe(void)
{
    sz mem_size = 16 * sizeof(struct pci_device);
    void *mem = kvalloc_alloc(sizeof(struct pci_device) * 16, alignof(struct pci_device));
    if (!mem)
        return result_error(ENOMEM);
    struct arena arn = arena_new(byte_array_new(mem, mem_size));

    struct dlist device_list;
    dlist_init_empty(&device_list);

    // NOTE: `u16` is used for `bus` so that we can compare it to `PCI_MAX_BUSSES` (which is 256).
    for (u16 bus = 0; bus < PCI_MAX_BUSSES; bus++) {
        for (u8 device = 0; device < PCI_MAX_DEVICES; device++) {
            // NOTE: We are only testing function 0 here. And overall this is _very_ basic.
            if (!pci_device_exists(bus, device, 0))
                continue;

            struct result_u8 header_type_res = pci_config_read8(bus, device, 0, PCI_OFFSET_HEADER_TYPE);
            if (header_type_res.is_error) {
                kvalloc_free(mem, mem_size);
                return result_error(header_type_res.code);
            }

            // Assume that this is safe given that the device exists.
            u16 vendor_id = result_u16_checked(pci_config_read16(bus, device, 0, PCI_OFFSET_VENDOR_ID));
            u16 device_id = result_u16_checked(pci_config_read16(bus, device, 0, PCI_OFFSET_DEVICE_ID));
            u8 class_code = result_u8_checked(pci_config_read8(bus, device, 0, PCI_OFFSET_CLASS_CODE));
            u8 subclass = result_u8_checked(pci_config_read8(bus, device, 0, PCI_OFFSET_SUBCLASS));
            u8 prog_if = result_u8_checked(pci_config_read8(bus, device, 0, PCI_OFFSET_PROG_IF));
            u8 revision_id = result_u8_checked(pci_config_read8(bus, device, 0, PCI_OFFSET_REVISION_ID));

            // Check if this is a general device (this is the only thing supported for now).
            if ((result_u8_checked(header_type_res) & PCI_MASK_HEADER_TYPE) != 0) {
                print_dbg(
                    STR("Skipping device %hhx:%hhx.%hhx [%hx:%hx] because its header is not type 0 (general device)\n"),
                    bus, device, 0, vendor_id, device_id);
                continue;
            }

            u8 interrupt_line = result_u8_checked(pci_config_read8(bus, device, 0, PCI_OFFSET_HDR0_INTERRUPT_LINE));

            // Allocate and link the device. All uninitialized will be zero because the arena returns
            // zero-initialized memory. Also the arena will crash before returning a NULL pointer.
            struct pci_device *dev = arena_alloc_aligned(&arn, sizeof(*dev), alignof(*dev));
            dev->vendor_id = vendor_id;
            dev->device_id = device_id;
            dev->class_code = class_code;
            dev->subclass = subclass;
            dev->prog_if = prog_if;
            dev->revision_id = revision_id;

            dev->bus = bus;
            dev->device = device;
            dev->func = 0;

            dev->interrupt_line = interrupt_line;

            struct result res = pci_get_resource_info(bus, device, 0, &dev->bars);
            if (res.is_error) {
                kvalloc_free(mem, mem_size);
                return res;
            }

            dlist_insert(&device_list, &dev->device_list);

            print_dbg(STR("Inserted device %hhx:%hhx.%hhx [%hx:%hx] into device list\n"), bus, device, 0, vendor_id,
                      device_id);
            for (i32 i = 0; i < PCI_MAX_BARS; i++) {
                if (dev->bars[i].used) {
                    print_dbg(STR("BAR%d: base=0x%lx, len=0x%lx (%s)\n"), i, dev->bars[i].base, dev->bars[i].len,
                              dev->bars[i].type == PCI_BAR_TYPE_IO ? STR("IO") : STR("MEM"));
                }
            }
        }
    }

    struct dlist *orig = &device_list;
    struct dlist *dev_iter = orig->next;
    struct pci_device *dev = NULL;

    while (dev_iter != orig) {
        dev = __container_of(dev_iter, struct pci_device, device_list);

        print_dbg(STR("Looking up drivers for device %hhx:%hhx.%hhx [%hx:%hx]\n"), dev->bus, dev->device, dev->func,
                  dev->vendor_id, dev->device_id);

        struct pci_device_driver *drv = pci_lookup_driver(dev->vendor_id, dev->device_id);

        if (drv) {
            print_dbg(STR("Probing driver %s [%hx:%hx]\n"), drv->name, dev->vendor_id, dev->device_id);

            struct result res = pci_set_driver_capabilities(dev, drv->capabilities);
            if (res.is_error) {
                kvalloc_free(mem, mem_size);
                return res;
            }

            assert(drv->probe);
            dev->driver = drv;
            res = drv->probe(dev);
            if (res.is_error) {
                kvalloc_free(mem, mem_size);
                return res;
            }
        } else {
            print_dbg(STR(" ... no single driver found (either zero or more than one)\n"));
        }

        dev_iter = dev_iter->next;
    }

    print_dbg(STR("Successfully probed all PCI devices\n"));

    return result_ok();
}
