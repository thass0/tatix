#include <config.h>
#include <tx/archive.h>
#include <tx/arena.h>
#include <tx/assert.h>
#include <tx/base.h>
#include <tx/buddy.h>
#include <tx/byte.h>
#include <tx/com.h>
#include <tx/gdt.h>
#include <tx/idt.h>
#include <tx/isr.h>
#include <tx/kvalloc.h>
#include <tx/net/arp.h>
#include <tx/net/ethernet.h>
#include <tx/net/icmp.h>
#include <tx/net/ip.h>
#include <tx/net/ip_addr.h>
#include <tx/net/netdev.h>
#include <tx/net/tcp.h>
#include <tx/paging.h>
#include <tx/pci.h>
#include <tx/print.h>
#include <tx/ramfs.h>
#include <tx/rtcfg.h>
#include <tx/sched.h>
#include <tx/time.h>
#include <tx/web.h>

extern char _rootfs_archive_start[];
extern char _rootfs_archive_end[];

static byte init_kernel_stack[TASK_STACK_SIZE] __used;

__noreturn void kernel_init(void);

__noreturn __naked void _kernel_init(void)
{
    __asm__ volatile("movl $init_kernel_stack + " TOSTRING(TASK_STACK_SIZE) " - 1, %esp\n"
                     "call kernel_init\n");
}

///////////////////////////////////////////////////////////////////////////////
// .entry section                                                            //
///////////////////////////////////////////////////////////////////////////////

__section(".entry.data") __aligned(0x1000) static struct pt pml4; // Single PML4 table
__section(".entry.data") __aligned(0x1000) static struct pt pdpt; // Single PDP table, this will have two entries
__section(".entry.data") __aligned(0x1000) static struct pt pd_id; // PD table for identity mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_id[16]; // PT pages for identity mapping (32 MB)
__section(".entry.data") __aligned(0x1000) static struct pt pd_vmem; // PD table for virtual mapping
__section(".entry.data") __aligned(0x1000) static struct pt pt_vmem[16]; // PT pages for virtual mapping (32 MB)

__section(".entry.text") __noreturn void _start(void)
{
    // Initialize a small page table. This page table identity-maps the first 32 MB of memory. That includes
    // where the current execution is at. Additionally, this page tables maps 32 MB starting at address
    // `KERN_BASE_VADDR` to the same first 32 MB of memory. The kernel uses virtual addresses with
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

static void mem_init(void)
{
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

    // First, initialize paging.
    struct byte_array dyn = paging_init(code_addrs, dyn_addrs);

    // Then initialize the kernel virtual memory allocator.
    struct result res = kvalloc_init(dyn);
    assert(!res.is_error);

    print_dbg(PINFO, STR("Initialized paging and virtual memory allocator\n"));
}

static void ram_fs_selftest(void)
{
    struct byte_array tmp_ba = option_byte_array_checked(kvalloc_alloc(5 * BIT(20), alignof(void *)));
    struct arena tmp = arena_new(tmp_ba);
    ram_fs_run_tests(tmp);
    kvalloc_free(tmp_ba);
}

static struct ram_fs *ram_fs_init(void)
{
    ram_fs_selftest();

    struct alloc rfs_alloc;
    rfs_alloc.a_ptr = NULL;
    rfs_alloc.alloc = kvalloc_alloc_wrapper;
    rfs_alloc.free = kvalloc_free_wrapper;

    struct ram_fs *rfs = ram_fs_new(rfs_alloc);
    assert(rfs);

    print_dbg(PINFO, STR("Initialized RAM file system\n"));

    return rfs;
}

static inline void ipv4_addr_selftest(void)
{
    ipv4_test_addr_parse(arena_new(option_byte_array_checked(kvalloc_alloc(0x2000, 64))));
}

static void net_init(struct runtime_config *cfg, struct arena arn)
{
    ipv4_addr_selftest();

    struct ipv4_addr host_ip = option_ipv4_addr_checked(cfg->host_ip);
    struct ipv4_addr default_gateway_ip = option_ipv4_addr_checked(cfg->default_gateway_ip);
    struct ipv4_addr local_ip = option_ipv4_addr_checked(cfg->local_ip);
    struct ipv4_addr local_ip_mask = option_ipv4_addr_checked(cfg->local_ip_mask);

    // This is the default route for all IP addresses that are outside of the local network (see below).
    struct ipv4_route_entry default_route;
    default_route.dest = ipv4_addr_new(0, 0, 0, 0);
    default_route.mask = ipv4_addr_new(0, 0, 0, 0);
    default_route.gateway = default_gateway_ip;
    default_route.interface = host_ip;
    ipv4_route_add(default_route);

    // This is a route to all local IP addresses. Other hosts on the virtual network with this host and the VM host
    // can be reached this way.
    struct ipv4_route_entry local_route;
    local_route.dest = local_ip;
    local_route.mask = local_ip_mask;
    local_route.gateway = host_ip;
    local_route.interface = host_ip;
    ipv4_route_add(local_route);

    // Initialize the `netdev` subsystem.
    netdev_set_default_ip_addr(host_ip);
    assert(!netdev_init_input_queue().is_error);

    print_dbg(PINFO, STR("Initialized IP networking: host=%s default_gateway=%s local=%s/%ld\n"),
              ipv4_addr_format(host_ip, &arn), ipv4_addr_format(default_gateway_ip, &arn),
              ipv4_addr_format(local_ip, &arn), ipv4_mask_prefix_length(local_ip_mask));
}

static void print_hello_message(struct ram_fs *rfs, struct arena tmp)
{
    assert(rfs);

    struct result_ram_fs_node node_res = ram_fs_open(rfs->root, STR("/hello.txt"));
    assert(!node_res.is_error);
    struct ram_fs_node *node = result_ram_fs_node_checked(node_res);
    struct byte_buf bbuf = byte_buf_from_array(byte_array_from_arena(500, &tmp));
    struct result_sz read_res = ram_fs_read(node, &bbuf, 0);
    assert(!read_res.is_error);

    print_str(str_from_byte_buf(bbuf));

    struct str_buf sbuf = str_buf_from_arena(&tmp, 128);
    print_fmt(sbuf, STR("Commit: %s\n"), STR(GIT_COMMIT));
}

static void handle_timer_interrupt(struct trap_frame *cpu_state __unused, void *private_data __unused)
{
    return;
}

// Defined below:
static void main(struct ram_fs *rfs, struct runtime_config *rtcfg);

__noreturn void kernel_init(void)
{
    assert(!isr_register_handler(0x20, handle_timer_interrupt, NULL).is_error);

    gdt_init();

    assert(!com_init(COM1_PORT).is_error);

    interrupt_init();

    time_init();

    mem_init();

    struct byte_array tmp_ba = option_byte_array_checked(kvalloc_alloc(0x2000, 64));
    struct arena tmp = arena_new(tmp_ba);

    sched_init();

    struct ram_fs *rfs = ram_fs_init();
    assert(rfs);

    struct byte_view rootfs_archive = byte_view_new(_rootfs_archive_start, _rootfs_archive_end - _rootfs_archive_start);
    assert(!archive_extract(rootfs_archive, rfs).is_error);

    struct result_runtime_config rtcfg_res = rtcfg_read_config(rfs, STR("/config.txt"), tmp);
    assert(!rtcfg_res.is_error);
    struct runtime_config *rtcfg = result_runtime_config_checked(rtcfg_res);
    assert(rtcfg);

    net_init(rtcfg, tmp);

    assert(!pci_probe().is_error);

    assert(!tcp_init().is_error);

    print_hello_message(rfs, tmp);

    kvalloc_free(tmp_ba);

    main(rfs, rtcfg);

    halt_execution();
}

///////////////////////////////////////////////////////////////////////////////
// Main loop with tasks                                                      //
///////////////////////////////////////////////////////////////////////////////

static void task_net_ping(void *ctx_ptr __unused)
{
    struct result res = result_ok();
    struct byte_array tmp_ba = option_byte_array_checked(kvalloc_alloc(0x2000, 64));
    struct arena tmp = arena_new(tmp_ba);
    struct byte_array sb_ba = option_byte_array_checked(kvalloc_alloc(0x4000, 64));
    struct send_buf sb = send_buf_new(arena_new(sb_ba));

    for (i32 i = 0; i < 5; i++) {
        res = icmpv4_send_echo(ipv4_addr_new(8, 8, 8, 8), 0xcafe, 0xcafe, sb, tmp);
        if (!res.is_error || res.code != EAGAIN)
            break;
        sleep_ms(time_ms_new(2000));
    }
    if (res.is_error)
        print_dbg(PINFO, STR("Ping failed\n"));
    else
        print_dbg(PINFO, STR("Ping succeeded\n"));

    kvalloc_free(sb_ba);
    kvalloc_free(tmp_ba);
}

static void task_tcp_poll_retransmit(void *ctx_ptr __unused)
{
    struct arena tmp = arena_new(option_byte_array_checked(kvalloc_alloc(0x2000, 64)));

    while (true) {
        struct result res = tcp_poll_retransmit(tmp);
        if (res.is_error)
            print_dbg(PDBG, STR("Error in TCP retransmission: %hu\n"), res.code);
        sleep_ms(time_ms_new(10));
    }
}

struct task_ctx_net_receive {
    struct arena tmp;
    struct send_buf sb;
};

static void task_net_receive(void *ctx_ptr)
{
    assert(ctx_ptr);
    struct task_ctx_net_receive *ctx = ctx_ptr;

    struct result res = result_ok();
    struct input_packet *in_packet = NULL;

    while (true) {
        in_packet = netdev_get_input();
        if (in_packet) {
            if (in_packet->n_failed_to_handle > 5) {
                netdev_release_input(in_packet);
                continue;
            }

            send_buf_clear(&ctx->sb);

            switch (in_packet->proto) {
            case NETDEV_PROTO_ARP:
                res = arp_handle_packet(in_packet, ctx->sb, ctx->tmp);
                break;
            case NETDEV_PROTO_IPV4:
                res = ipv4_handle_packet(in_packet, ctx->sb, ctx->tmp);
                break;
            default:
                print_dbg(PINFO, STR("Received packet with unknown protocol 0x%hx. Dropping ...\n"), in_packet->proto);
                break;
            }

            // Release the packet if the handler returned a sucess. Otherwise, leave the packet in the queue
            // so that it can be handled again.
            if (res.is_error) {
                print_dbg(PWARN, STR("Failed to handle packet 0x%lx. Total attempts: %ld\n"), in_packet,
                          in_packet->n_failed_to_handle);
                in_packet->n_failed_to_handle++;
            } else {
                netdev_release_input(in_packet);
            }
        }

        sleep_ms(time_ms_new(10));
    }
}

struct task_ctx_web_listen {
    struct ipv4_addr addr;
    u16 port;
    struct ram_fs_node *root;
};

static void task_web_listen(void *ctx_ptr)
{
    assert(ctx_ptr);
    struct task_ctx_web_listen *ctx = ctx_ptr;
    web_listen(ctx->addr, ctx->port, ctx->root);
}

static void main(struct ram_fs *rfs, struct runtime_config *rtcfg)
{
    assert(rfs);
    assert(rtcfg);

    sched_create_task(task_net_ping, NULL);

    sched_create_task(task_tcp_poll_retransmit, NULL);

    struct task_ctx_net_receive recv_ctx;
    recv_ctx.tmp = arena_new(option_byte_array_checked(kvalloc_alloc(0x2000, 64)));
    recv_ctx.sb = send_buf_new(arena_new(option_byte_array_checked(kvalloc_alloc(0x4000, 64))));
    sched_create_task(task_net_receive, &recv_ctx);

    struct result_ram_fs_node web_res = ram_fs_open(rfs->root, STR("/web/"));
    assert(!web_res.is_error);
    struct ram_fs_node *web_dir = result_ram_fs_node_checked(web_res);
    assert(web_dir->type == RAM_FS_TYPE_DIR);
    struct task_ctx_web_listen web_ctx;
    web_ctx.addr = option_ipv4_addr_checked(rtcfg->host_ip);
    web_ctx.port = 80;
    web_ctx.root = web_dir;
    sched_create_task(task_web_listen, &web_ctx);

    while (true)
        sleep_ms(time_ms_new(1000));
}
