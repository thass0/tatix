#ifndef __TX_PCI_H__
#define __TX_PCI_H__

#include <tx/base.h>
#include <tx/list.h>
#include <tx/string.h>

///////////////////////////////////////////////////////////////////////////////
// Constants                                                                 //
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// Structures and types                                                      //
///////////////////////////////////////////////////////////////////////////////

enum pci_bar_type {
    PCI_BAR_TYPE_MEM,
    PCI_BAR_TYPE_IO,
};

#define PCI_BAR_FLAG_PREFETCHABLE BIT(0)

// Parsed representation of the information found in BARs.
struct pci_bar {
    bool used;
    enum pci_bar_type type;
    u16 flags;
    u64 base;
    u64 len;
};

#define PCI_MAX_BARS 6

struct pci_device {
    struct dlist device_list;

    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision_id;
    u8 bus;
    u8 device;
    u8 func;

    struct pci_bar bars[PCI_MAX_BARS];

    struct pci_device_driver *driver;
};

struct pci_device_id {
    u16 vendor;
    u16 device;
};

typedef struct result (*pci_device_driver_probe_func_t)(struct pci_device *dev);

// NOTE: Whenever the alignment changes, the alignment of the driver table in the linker script must also change.
struct __aligned(8) pci_device_driver {
    struct str name;
    sz n_ids;
    struct pci_device_id *ids;
    pci_device_driver_probe_func_t probe;
} __packed;

///////////////////////////////////////////////////////////////////////////////
// PCI functions                                                             //
///////////////////////////////////////////////////////////////////////////////

// This is all stuff that drivers can use as they see fit.

// Read data from the PCI configuration space. An error will be returned if the device doesn't exist.

// `offset` must be four-byte aligned.
struct result_u32 pci_config_read32(u8 bus, u8 device, u8 func, u8 offset);
// `offset` must be two-byte aligned.
struct result_u16 pci_config_read16(u8 bus, u8 device, u8 func, u8 offset);
// No alignment constraints.
struct result_u8 pci_config_read8(u8 bus, u8 device, u8 func, u8 offset);

// Write data to the PCI configuration space. If the device doesn't exist, the write will go through
// but all data will be discarded. There is no reliable way to tell if a write succeeded because, not
// all devices return the value that was previously written when reading from a register (consider for
// example how the size associated with a BAR is retrieved).

// `offset` must be four-byte aligned.
void pci_config_write32(u8 bus, u8 device, u8 func, u8 offset, u32 value);
// `offset` must be two-byte aligned.
void pci_config_write16(u8 bus, u8 device, u8 func, u8 offset, u16 value);
// No alignment constraints.
void pci_config_write8(u8 bus, u8 device, u8 func, u8 offset, u8 value);

// Initialize the PCI subsystem by probing all devices. This function will call `driver_probe` on
// all drivers that are registered with the PCI subsystem if a PCI device with vendor and device
// IDs matching those requested by a driver.
struct result pci_probe(void);

///////////////////////////////////////////////////////////////////////////////
// Static driver registration                                                //
///////////////////////////////////////////////////////////////////////////////

// The `PCI_REGISTER_DRIVER` macro inserts a pointer to a `struct pci_device_driver` into a static table
// of device drivers known to the PCI subsystem.

#define PCI_REGISTER_DRIVER(_name, _n_ids, _ids, _probe)                                                           \
    __used __section(                                                                                              \
        ".pci_device_driver_list_" STRINGIFY(_name)) static struct pci_device_driver pci_device_driver_##_name = { \
        .name = STR_STATIC(STRINGIFY(_name)),                                                                      \
        .n_ids = _n_ids,                                                                                           \
        .ids = _ids,                                                                                               \
        .probe = _probe,                                                                                           \
    }

#endif // __TX_PCI_H__
