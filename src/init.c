#include <config.h>
#include <tx/arena.h>
#include <tx/assert.h>
#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/bytes.h>
#include <tx/com.h>
#include <tx/gdt.h>
#include <tx/idt.h>
#include <tx/isr.h>
#include <tx/paging.h>
#include <tx/print.h>

__section(".entry.data") __aligned(0x1000) static struct pt pml4; // Single PML4 table
__section(".entry.data") __aligned(0x1000) static struct pt pdpt; // Single PDP table, this will have two entries
__section(".entry.data") __aligned(0x1000) static struct pt pd_id; // PD table for identity mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_id[8]; // PT pages for identity mapping (16 MB)
__section(".entry.data") __aligned(0x1000) static struct pt pd_vmem; // PD table for virtual mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_vmem[8]; // PT pages for virtual mapping (16 MB)

static byte init_kernel_stack[0x2000] __used;

__noreturn void kernel_init(void);

__noreturn __naked void _kernel_init(void)
{
    __asm__ volatile("movl $init_kernel_stack, %esp\n"
                     "call kernel_init\n");
}

__section(".entry.text") __noreturn void _start(void)
{
    // Initialize a small page table. This page table identity-maps the first 16 MB of memory. That includes
    // where the current execution is at. Additionally, this page tables maps 16 MB starting at address
    // `KERN_BASE_VADDR` to the same first 16 MB of memory. The kernel uses virtual addresses with
    // `KERN_BAES_VADDR` as their base. So once the page table is set, we can jump into the part of the
    // kernel that uses virtual addresses. Refer to the linker script kernel.ld for more detail.

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

union __aligned(0x2000) kernel_stack
{
    struct proc *proc;
    byte stack[0x2000];
};

typedef u32 pid_t;

struct context {
    u64 rax;
};

struct proc {
    struct vas vas;
    union kernel_stack *kstack;
    struct trap_frame *trap_frame;
    struct context *context;
    pid_t pid;
    struct arena *arn;
};

#define MAX_PID 1024
static struct proc proc_list[MAX_PID];
static pid_t proc_list_len;

struct proc *proc_alloc_pid(void)
{
    struct proc *proc = NULL;

    if (proc_list_len == MAX_PID)
        return NULL;

    proc = &proc_list[proc_list_len];
    proc->pid = proc_list_len;
    proc_list_len++;
    return proc;
}

extern char _init_proc_start[];
extern char _init_proc_end[];

__naked __noreturn void trapret(void)
{
    __asm__ volatile("mov %rsp, %rdi");
    __asm__ volatile("push %0" : : "r"((ptr)isr_return));
    __asm__ volatile("jmp handle_interrupt");
}

void proc_init(struct buddy *phys_alloc, struct arena *arn)
{
    struct proc *proc = proc_alloc_pid();
    assert(proc); /* TODO: If all PIDs are used, we should try to purge unused processes */
    proc->arn = arn;

    union kernel_stack *kern_stack = arena_alloc_aligned(arn, sizeof(union kernel_stack), alignof(union kernel_stack));
    kern_stack->proc = proc;
    proc->kstack = kern_stack;
    struct task_state *ts = tss_get_global();
    ptr rsp = (u64)kern_stack + sizeof(union kernel_stack) - 8;
    ts->rsp0 = rsp;
    print_dbg(STR("***** Kernel stack: 0x%lx\n"), rsp);

    rsp -= sizeof(*proc->trap_frame);
    proc->trap_frame = (struct trap_frame *)rsp;

    proc->trap_frame->r11 = 0xdeadbeef;
    proc->trap_frame->vector = 0x80;

    // Set up the context that we'll execute from
    rsp -= sizeof(ptr *);
    *(ptr *)rsp = (ptr)trapret;

    // rsp -= sizeof(*proc->context);
    // proc->context = (struct context *)rsp;
    // proc->context->rip = forkret;

    proc->vas = vas_new(pt_alloc_page_table(arn), phys_alloc);

    // Map the kernel virtual addresses into the process.
    // TODO: This should be kernel-only memory. It uses PT_FLAG_US right now because I couldn't
    // get the code to work otherwise. But this is not correct! I need to fix this and remove PT_FLAG_US
    for (sz offset = 0; offset < 0x800000; offset += PAGE_SIZE)
        assert(pt_map(proc->vas.pt, KERN_BASE_VADDR + offset, KERN_BASE_PADDR + offset,
                      PT_FLAG_P | PT_FLAG_RW | PT_FLAG_US) == 0);

    struct vma user_code = vma_new(0x100000, 0x16000);
    assert(vas_map(proc->vas, user_code, PT_FLAG_P | PT_FLAG_RW | PT_FLAG_US) == 0);
    assert(vas_memcpy(proc->vas, user_code, bytes_new(_init_proc_start, _init_proc_end - _init_proc_start)) == 0);
    proc->trap_frame->rip = user_code.base;

    proc->trap_frame->cs = segment_selector(SEG_IDX_USER_CODE, SEG_DESC_DPL_USER);
    proc->trap_frame->ss = segment_selector(SEG_IDX_USER_DATA, SEG_DESC_DPL_USER); // TODO: Probably not needed
    struct vma user_stack = vma_new(0x200000, 0x2000);
    assert(vas_map(proc->vas, user_stack, PT_FLAG_P | PT_FLAG_RW | PT_FLAG_US) == 0);
    proc->trap_frame->rsp = user_stack.base + user_stack.len - 1;
    proc->trap_frame->rbp = 0;

    print_str(STR("*** Processes setup has worked\n"));

    load_cr3(pt_walk(current_page_table(), (vaddr_t)proc->vas.pt.pml4));

    print_str(STR("*** I did the mapping correctly :^)\n"));

    print_dbg(STR("Stack top before: 0x%lx\n"), rsp);
    // Switch stacks and return. The return value is taken from the top of the new stack.
    __asm__ volatile("mov %0, %%rsp" : : "r"(rsp) : "memory");
    __asm__ volatile("ret");
}

__noreturn void kernel_init(void)
{
    struct arena arn = arena_new((void *)KERN_DATA_VADDR, KERN_DATA_LEN);

    gdt_init();
    com_init(COM1_PORT);
    interrupt_init();

    print_str(STR(" ______   ______    ______   __    __  __\n"
                  "/\\__  _\\ /\\  __ \\  /\\__  _\\ /\\ \\  /\\_\\_\\_\\\n"
                  "\\/_/\\ \\/ \\ \\  __ \\ \\/_/\\ \\/ \\ \\ \\ \\/_/\\_\\/_\n"
                  "   \\ \\_\\  \\ \\_\\ \\_\\   \\ \\_\\  \\ \\_\\  /\\_\\/\\_\\\n"
                  "    \\/_/   \\/_/\\/_/    \\/_/   \\/_/  \\/_/\\/_/\n"));

    struct buddy *phys_alloc = buddy_init(bytes_new((void *)KERN_PHYS_PADDR, KERN_PHYS_LEN), &arn);
    // struct buddy *phys_alloc = NULL;
    proc_init(phys_alloc, &arn);

    __asm__ volatile("int $34"); /* Just to see if interrupts work :) */

    // void *ptr = NULL;
    // arena_alloc(&arn, 8);
    // struct bytes area = bytes_from_arena(0x11490, &arn);
    // struct buddy *buddy = buddy_init(area, &arn);
    // assert((ptr = buddy_alloc(buddy, 0)) && ptr == (void *)0x80301000);
    // buddy_free(buddy, ptr, 0);
    // assert((ptr = buddy_alloc(buddy, 1)) && ptr == (void *)0x80301000);
    // buddy_free(buddy, ptr, 1);
    // assert((ptr = buddy_alloc(buddy, 2)) && ptr == (void *)0x80301000);
    // assert((ptr = buddy_alloc(buddy, 1)) && ptr == (void *)0x80305000);
    // buddy_free(buddy, ptr, 1);
    // assert((ptr = buddy_alloc(buddy, 2)) && ptr == (void *)0x80305000);

    // print_str(STR("*** Worked, all assertions succeeded\n"));

    // struct vas vas = vas_new(0x400000, 0x40000, &arn);
    // int rc = vas_map(vas, (struct vma){ .base = 0x2000, .len = 0x3000 });
    // print_dbg(STR("rc=%d\n"), rc);
    // assert(vas_unmap(vas, (struct vma){ .base = 0x2000, .len = 0x3000 }) == 0);

    hlt();
}
