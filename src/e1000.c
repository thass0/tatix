// Driver for Intel 82540EM (called e1000 in Linux and QEMU).

#include <tx/asm.h>
#include <tx/base.h>
#include <tx/bytes.h>
#include <tx/pci.h>
#include <tx/print.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

#define E1000_NUM_SUPPORTED_IDS 1

static struct pci_device_id supported_ids[E1000_NUM_SUPPORTED_IDS] = {
    { E1000_VENDOR_ID, E1000_DEVICE_ID },
};

static struct result e1000_probe(struct pci_device *dev)
{
    assert(dev);
    print_dbg(STR("Hello, I'm the e1000 driver!\n"));
    return result_ok();
}

PCI_REGISTER_DRIVER(e1000, E1000_NUM_SUPPORTED_IDS, supported_ids, e1000_probe);
