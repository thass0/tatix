#include <config.h>
#include <tx/elf64.h>
#include <tx/error.h>
#include <tx/gdt.h>
#include <tx/kvalloc.h>
#include <tx/pool.h>
#include <tx/proc.h>

// TODO(sync): Protect proc_list from concurrent access.

#define PROC_MAX_PID 1024
static struct proc proc_list[PROC_MAX_PID];
static pid_t proc_list_len;

#define PROC_MEM_SIZE 0x20000 /* TODO: This is very little. Increase later. */
#define PROC_LOAD_BASE ((vaddr_t)0x400000)
#define PROC_LOAD_SIZE 0x100000 /* 1MiB */
#define PROC_STACK_BASE ((vaddr_t)0x500000)
#define PROC_STACK_SIZE 0x4000 /* 16KiB */

static __naked __noreturn void proc_trapret(void)
{
    __asm__ volatile("mov %rsp, %rdi");
    __asm__ volatile("push %0" : : "r"((ptr)isr_return));
    __asm__ volatile("jmp handle_interrupt");
}

///////////////////////////////////////////////////////////////////////////////
// ELF loading                                                               //
///////////////////////////////////////////////////////////////////////////////

static struct result_ptr elf_load(struct proc *proc, struct bytes elf_bytes)
{
    if (elf_bytes.len < sizeof(struct elf64_hdr))
        return result_ptr_error(EINVAL);

    struct elf64_hdr *elf_hdr = (struct elf64_hdr *)elf_bytes.dat;
    if (!elf64_is_valid(elf_hdr))
        return result_ptr_error(EINVAL);

    // `phdr_count` is a `u16` so it can't be too big for the multiply.
    if (ADD_OVERFLOW(elf_hdr->phdr_tab_offset, sizeof(struct elf64_phdr) * (sz)elf_hdr->phdr_count))
        return result_ptr_error(EINVAL);
    if (elf_hdr->phdr_tab_offset >= (u64)elf_bytes.len ||
        elf_hdr->phdr_tab_offset + sizeof(struct elf64_phdr) * (sz)elf_hdr->phdr_count > (u64)elf_bytes.len)
        return result_ptr_error(EINVAL);

    struct elf64_phdr *phdr = (struct elf64_phdr *)(elf_bytes.dat + elf_hdr->phdr_tab_offset);
    struct elf64_phdr *phdr_end = phdr + elf_hdr->phdr_count;
    u64 load_size = 0;
    struct vma vma = vma_new(0, 0);
    struct result res = result_ok();
    bool loaded_entry = false;

    for (; phdr < phdr_end; phdr++) {
        if (phdr->type != PT_LOAD)
            continue;

        load_size = MAX(phdr->file_size, phdr->mem_size); // Alignment mandated by VAS functions.
        if (!load_size)
            continue; // Skip all the hassle if the segment is empty.

        if (ADD_OVERFLOW(phdr->offset, load_size) || ADD_OVERFLOW(phdr->offset + load_size, (u64)elf_bytes.dat) ||
            ADD_OVERFLOW((u64)phdr->vaddr, load_size))
            return result_ptr_error(EINVAL);
        if (phdr->offset + phdr->file_size > (u64)elf_bytes.len)
            return result_ptr_error(EINVAL);

        if (!IN_RANGE(phdr->vaddr, PROC_LOAD_BASE, PROC_LOAD_SIZE) ||
            !IN_RANGE(phdr->vaddr + load_size, PROC_LOAD_BASE, PROC_LOAD_SIZE))
            return result_ptr_error(EINVAL);

        vma = vma_new(phdr->vaddr, load_size);

        res =
            vas_map(proc->vas, vma, PT_FLAG_P | PT_FLAG_RW | PT_FLAG_US); // TODO: Set flags depending on `phdr->flags`.
        if (res.is_error)
            return result_ptr_error(res.code);

        res = vas_memset(proc->vas, vma, 0);
        if (res.is_error)
            return result_ptr_error(res.code);

        res = vas_memcpy(proc->vas, vma, bytes_new(elf_bytes.dat + phdr->offset, phdr->file_size));
        if (res.is_error)
            return result_ptr_error(res.code);

        if (IN_RANGE((u64)elf_hdr->entry, phdr->vaddr, load_size))
            loaded_entry = true;
    }

    if (!loaded_entry)
        return result_ptr_error(EINVAL);
    return result_ptr_ok((ptr)elf_hdr->entry);
}

///////////////////////////////////////////////////////////////////////////////
// Process creation                                                          //
///////////////////////////////////////////////////////////////////////////////

static struct proc *alloc_pid(void)
{
    struct proc *proc = NULL;

    if (proc_list_len == PROC_MAX_PID)
        return NULL;

    proc = &proc_list[proc_list_len];
    proc->pid = proc_list_len;
    proc_list_len++;
    return proc;
}

// Wrapper around pool allocation so the pool allocator can be used with `struct alloc` from paging.
static void *pool_alloc_wrapper(void *pool_ptr, sz size, sz align)
{
    assert(pool_ptr);
    struct pool *pool = pool_ptr;
    assert(align == PAGE_SIZE);
    assert(size == PAGE_SIZE);
    assert(pool->size == size);
    return pool_alloc(pool);
}

static void pool_free_wrapper(void *pool_ptr, void *ptr, sz size)
{
    assert(pool_ptr);
    struct pool *pool = pool_ptr;
    assert(size == PAGE_SIZE);
    assert(pool->size == size);
    pool_free(pool, ptr);
}

static struct result proc_init_vas(struct vas *vas)
{
    void *mem = kvalloc_alloc(PROC_MEM_SIZE, PAGE_SIZE);
    struct pool *pool = kvalloc_alloc(sizeof(*pool), alignof(*pool));
    if (!mem || !pool)
        return result_error(ENOMEM);
    *pool = pool_new(bytes_new(mem, PROC_MEM_SIZE), PAGE_SIZE);

    struct alloc alloc = alloc_new(pool, pool_alloc_wrapper, pool_free_wrapper);
    *vas = vas_new(current_page_table(), alloc);

    return result_ok();
}

struct proc *proc_create(struct bytes exec)
{
    struct proc *proc = alloc_pid();
    if (!proc)
        return NULL; // TODO: Try to purge unused processes if all PIDs are used (or increase PROC_MAX_PID for now).

    // Set up initial context.
    proc->context = kvalloc_alloc(sizeof(*proc->context), alignof(*proc->context));
    if (!proc->context)
        return NULL;
    memset(bytes_new(proc->context, sizeof(*proc->context)), 0);

    // Create kernel stack and link it with the proc structure.
    union kernel_stack *kern_stack = kvalloc_alloc(sizeof(union kernel_stack), PAGE_SIZE);
    if (!kern_stack)
        return NULL;
    kern_stack->proc = proc;
    proc->kstack = kern_stack;

    // A trap frame is set up at the top of the kernel stack. It will be set up to make the new process think
    // it just returned from a system call when it's first executed.
    ptr stack_ptr = (ptr)kern_stack + sizeof(union kernel_stack) - 8;
    stack_ptr -= sizeof(*proc->trap_frame);
    proc->trap_frame = (struct trap_frame *)stack_ptr;

    // Initialize the registers whose values we already know (more are initialized later)
    for (sz i = 0; i < sizeof(*proc->trap_frame); i += 4)
        *((u32 *)((u8 *)proc->trap_frame + i)) = 0xdeadbeef; // Some recognizable pattern for debugging.
    proc->trap_frame->vector = IRQ_SYSCALL;
    proc->trap_frame->cs = segment_selector(SEG_IDX_USER_CODE, SEG_DESC_DPL_USER);
    proc->trap_frame->ss = segment_selector(SEG_IDX_USER_DATA, SEG_DESC_DPL_USER); // TODO: Probably not needed

    // Set up the return address.
    stack_ptr -= sizeof(ptr *);
    *(ptr *)stack_ptr = (ptr)proc_trapret;

    struct result res = proc_init_vas(&proc->vas);
    if (res.is_error)
        return NULL;

    // Load the ELF and set the instruction pointer to the ELF's entry point.
    struct result_ptr entry_res = elf_load(proc, exec);
    if (entry_res.is_error)
        return NULL;
    proc->trap_frame->rip = result_ptr_checked(entry_res);

    // Allocate user space stack for the process and set up its stack pointer.
    struct vma user_stack = vma_new(PROC_STACK_BASE, PROC_STACK_SIZE);
    res = vas_map(proc->vas, user_stack, PT_FLAG_P | PT_FLAG_RW | PT_FLAG_US);
    if (res.is_error)
        return NULL;
    proc->trap_frame->rsp = user_stack.base + user_stack.len - 1;
    proc->trap_frame->rbp = 0;

    proc->context->rsp = stack_ptr;

    return proc;
}
