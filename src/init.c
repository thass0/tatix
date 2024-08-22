#include <tx/assert.h>
#include <tx/base.h>
#include <tx/com.h>
#include <tx/idt.h>
#include <tx/print.h>

#define global_kernel_arena_buffer_size 10000
u8 global_kernel_arena_buffer[global_kernel_arena_buffer_size];

struct pte {
    u64 bits;
} __packed;

struct pt {
    struct pte entries[512];
} __packed;

__section(".entry.data") __aligned(0x1000) static struct pt pml4; // Single PML4 table
__section(".entry.data") __aligned(0x1000) static struct pt pdp; // Single PDP table, this will have two entries
__section(".entry.data") __aligned(0x1000) static struct pt pd_id; // PD table for identity mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_id[8]; // PT pages for identity mapping (16 MB)
__section(".entry.data") __aligned(0x1000) static struct pt pd_vmem; // PD table for virtual mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_vmem[8]; // PT pages for virtual mapping (16 MB)

#define PT_FLAG_P BIT(0)
#define PT_FLAG_RW BIT(1)

// Get the index that `vaddr` has in some page table page where `base` is the
// index of the first of the nine bits in `vaddr` that make up this index.
#define PT_IDX(vaddr, base) (((BIT(9) - 1) << (base)) & (vaddr)) >> (base)

#define PML4_BIT_BASE 39LU
#define PDP_BIT_BASE 30LU
#define PD_BIT_BASE 21LU
#define PT_BIT_BASE 12LU

#define PTE_REGION_SIZE BIT(PT_BIT_BASE)
#define PDE_REGION_SIZE BIT(PD_BIT_BASE)

#define KERN_BASE_VADDR 0x80100000LU

void kernel_init(void);

__section(".entry.text") __noreturn void _start(void)
{
    // Initialize a small page table. This page table identity-maps the first 16 MB of memory. That includes
    // where the current execution is at. Additionally, this page tables maps 16 MB starting at address
    // `KERN_BASE_VADDR` to the same first 16 MB of memory. The kernel uses virtual addresses with
    // `KERN_BAES_VADDR` as their base. So once the page table is set, we can jump into the part of the
    // kernel that uses virtual addresses. Refer to the linker script kernel.ld for more detail.

    pml4.entries[PT_IDX(0, PML4_BIT_BASE)].bits = (u64)&pdp | PT_FLAG_P | PT_FLAG_RW;
    pdp.entries[PT_IDX(0, PDP_BIT_BASE)].bits = (u64)&pd_id | PT_FLAG_P | PT_FLAG_RW;
    for (int i = 0; i < countof(pt_id); i++) {
        pd_id.entries[i].bits = (u64)&pt_id[i] | PT_FLAG_P | PT_FLAG_RW;
        for (int j = 0; j < countof(pt_id[i].entries); j++)
            pt_id[i].entries[j].bits = (i * PDE_REGION_SIZE + j * PTE_REGION_SIZE) | PT_FLAG_P | PT_FLAG_RW;
    }
    pdp.entries[PT_IDX(KERN_BASE_VADDR, PDP_BIT_BASE)].bits = (u64)&pd_vmem | PT_FLAG_P | PT_FLAG_RW;
    for (int i = 0; i < countof(pt_vmem); i++) {
        pd_vmem.entries[i].bits = (u64)&pt_vmem[i] | PT_FLAG_P | PT_FLAG_RW;
        for (int j = 0; j < countof(pt_vmem[i].entries); j++)
            pt_vmem[i].entries[j].bits = (i * PDE_REGION_SIZE + j * PTE_REGION_SIZE) | PT_FLAG_P | PT_FLAG_RW;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"((u64)&pml4) : "memory");

    kernel_init();

    hlt();
}

void kernel_init(void)
{
    com_init(COM1_PORT);
    interrupt_init();

    print_str(STR(" ______   ______    ______   __    __  __\n"
                  "/\\__  _\\ /\\  __ \\  /\\__  _\\ /\\ \\  /\\_\\_\\_\\\n"
                  "\\/_/\\ \\/ \\ \\  __ \\ \\/_/\\ \\/ \\ \\ \\ \\/_/\\_\\/_\n"
                  "   \\ \\_\\  \\ \\_\\ \\_\\   \\ \\_\\  \\ \\_\\  /\\_\\/\\_\\\n"
                  "    \\/_/   \\/_/\\/_/    \\/_/   \\/_/  \\/_/\\/_/\n"));

    __asm__ volatile("int $0x22");
    __asm__ volatile("int3");
}
