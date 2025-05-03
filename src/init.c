#include <config.h>
#include <tx/archive.h>
#include <tx/arena.h>
#include <tx/assert.h>
#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/byte.h>
#include <tx/com.h>
#include <tx/ethernet.h>
#include <tx/gdt.h>
#include <tx/idt.h>
#include <tx/isr.h>
#include <tx/kvalloc.h>
#include <tx/netdev.h>
#include <tx/paging.h>
#include <tx/pci.h>
#include <tx/print.h>
#include <tx/ramfs.h>

extern char _rootfs_archive_start[];
extern char _rootfs_archive_end[];

#define INIT_KERNEL_STACK_SIZE 0x4000
static byte init_kernel_stack[INIT_KERNEL_STACK_SIZE] __used;

__noreturn void kernel_init(void);

__noreturn __naked void _kernel_init(void)
{
    __asm__ volatile("movl $init_kernel_stack + " TOSTRING(INIT_KERNEL_STACK_SIZE) " - 1, %esp\n"
                     "call kernel_init\n");
}

///////////////////////////////////////////////////////////////////////////////
// .entry section                                                            //
///////////////////////////////////////////////////////////////////////////////

__section(".entry.data") __aligned(0x1000) static struct pt pml4; // Single PML4 table
__section(".entry.data") __aligned(0x1000) static struct pt pdpt; // Single PDP table, this will have two entries
__section(".entry.data") __aligned(0x1000) static struct pt pd_id; // PD table for identity mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_id[8]; // PT pages for identity mapping (16 MB)
__section(".entry.data") __aligned(0x1000) static struct pt pd_vmem; // PD table for virtual mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_vmem[8]; // PT pages for virtual mapping (16 MB)

__section(".entry.text") __noreturn void _start(void)
{
    // Initialize a small page table. This page table identity-maps the first 16 MB of memory. That includes
    // where the current execution is at. Additionally, this page tables maps 16 MB starting at address
    // `KERN_BASE_VADDR` to the same first 16 MB of memory. The kernel uses virtual addresses with
    // `KERN_BAES_VADDR` as their base. So once the page table is set, we can jump into the part of the
    // kernel that uses virtual addresses. Refer to the linker script kernel.ld for more detail.
    // NOTE: The identity mapping is only required until jumping into the kernel mapped with virtual addresses.

    pml4.entries[PT_IDX(0, PML4_BIT_BASE)].bits = (u64)&pdpt | PT_FLAG_P | PT_FLAG_RW;
    pdpt.entries[PT_IDX(0, PDPT_BIT_BASE)].bits = (u64)&pd_id | PT_FLAG_P | PT_FLAG_RW;
    for (int i = 0; i < countof(pt_id); i++) {
        pd_id.entries[i].bits = (u64)&pt_id[i] | PT_FLAG_P | PT_FLAG_RW;
        for (int j = 0; j < countof(pt_id[i].entries); j++)
            pt_id[i].entries[j].bits = (i * PDE_REGION_SIZE + j * PTE_REGION_SIZE) | PT_FLAG_P | PT_FLAG_RW;
    }
    pdpt.entries[PT_IDX(KERN_BASE_VADDR, PDPT_BIT_BASE)].bits = (u64)&pd_vmem | PT_FLAG_P | PT_FLAG_RW;
    for (int i = 0; i < countof(pt_vmem); i++) {
        pd_vmem.entries[i].bits = (u64)&pt_vmem[i] | PT_FLAG_P | PT_FLAG_RW;
        for (int j = 0; j < countof(pt_vmem[i].entries); j++)
            pt_vmem[i].entries[j].bits = (i * PDE_REGION_SIZE + j * PTE_REGION_SIZE) | PT_FLAG_P | PT_FLAG_RW;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"((u64)&pml4) : "memory");

    _kernel_init();
}

///////////////////////////////////////////////////////////////////////////////
// Kernel initialization                                                     //
///////////////////////////////////////////////////////////////////////////////

void ram_fs_selftest(void)
{
    struct byte_array test_arn_mem = option_byte_array_checked(kvalloc_alloc(5 * BIT(20), alignof(void *)));
    struct arena test_arn = arena_new(test_arn_mem);
    ram_fs_run_tests(test_arn);
}

void print_hello_txt(struct ram_fs *rfs)
{
    assert(rfs);

    struct result_ram_fs_node node_res = ram_fs_open(rfs, STR("/hello.txt"));
    assert(!node_res.is_error);
    struct ram_fs_node *node = result_ram_fs_node_checked(node_res);
    struct byte_buf bbuf = byte_buf_from_array(option_byte_array_checked(kvalloc_alloc(500, alignof(void *))));
    struct result_sz read_res = ram_fs_read(node, &bbuf, 0);
    assert(!read_res.is_error);

    print_str(str_from_byte_buf(bbuf));
}

static void handle_timer_interrupt(struct trap_frame *cpu_state __unused, void *private_data __unused)
{
    return;
}

__noreturn void kernel_init(void)
{
    isr_register_handler(0x20, handle_timer_interrupt, NULL);
    gdt_init();
    com_init(COM1_PORT);
    interrupt_init();

    // Set up fixed memory regions for paging init.
    assert(KERN_DYN_PADDR > KERN_BASE_PADDR && KERN_DYN_PADDR - KERN_BASE_PADDR == KERN_DYN_VADDR - KERN_BASE_VADDR);
    sz code_len = KERN_DYN_PADDR - KERN_BASE_PADDR;
    struct addr_mapping code_addrs;
    code_addrs.vbase = KERN_BASE_VADDR;
    code_addrs.pbase = KERN_BASE_PADDR;
    code_addrs.len = code_len;
    struct addr_mapping dyn_addrs;
    dyn_addrs.vbase = KERN_DYN_VADDR;
    dyn_addrs.pbase = KERN_DYN_PADDR;
    dyn_addrs.len = KERN_DYN_LEN;

    struct byte_array dyn = paging_init(code_addrs, dyn_addrs);

    // Initialize the kernel virtual memory allocator.
    struct result res = kvalloc_init(dyn);
    assert(!res.is_error);

    ram_fs_selftest();

    // Initialize the RAM file system.
    struct alloc rfs_alloc;
    rfs_alloc.a_ptr = NULL;
    rfs_alloc.alloc = kvalloc_alloc_wrapper;
    rfs_alloc.free = kvalloc_free_wrapper;
    struct ram_fs *rfs = ram_fs_new(rfs_alloc);
    assert(rfs);

    // Extract rootfs archive into the RAM fs.
    struct byte_view rootfs_archive = byte_view_new(_rootfs_archive_start, _rootfs_archive_end - _rootfs_archive_start);
    res = archive_extract(rootfs_archive, rfs);
    assert(!res.is_error);

    print_hello_txt(rfs);

    netdev_set_default_ip_addr(ipv4_addr_new(192, 168, 100, 2));

    res = pci_probe();
    assert(!res.is_error);

    struct arena arn = arena_new(option_byte_array_checked(kvalloc_alloc(ETHERNET_MAX_FRAME_SIZE, 64)));
    netdev_arp_scan(ipv4_addr_new(192, 168, 100, 1), arn);

    hlt();
}
