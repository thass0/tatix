#include <tx/base.h>
#include <tx/string.h>

#define SCRATCH_SPACE 0x10000
#define LOAD_COUNT 0x2000

#define IO_PORT_BASE 0x1f0
#define OFFSET_SECTOR_COUNT 2
#define OFFSET_LBA_LOW 3
#define OFFSET_LBA_MID 4
#define OFFSET_LBA_HIGH 5
#define OFFSET_LBA_EXTRA 6
#define OFFSET_STATUS 7
#define OFFSET_COMMAND 7

#define COMMAND_READ_PIO BIT(5)

#define STATUS_READY BIT(6)
#define STATUS_BUSY BIT(7)

#define SECTOR_SIZE 512

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ volatile("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline void insl(u16 port, void *addr, u32 cnt)
{
    __asm__ volatile("cld; rep insl" : "=D"(addr), "=c"(cnt) : "d"(port), "0"(addr), "1"(cnt) : "memory", "cc");
}

static inline void stosb(void *addr, u32 data, u32 cnt)
{
    __asm__ volatile("cld; rep stosb" : "=D"(addr), "=c"(cnt) : "0"(addr), "1"(cnt), "a"(data) : "memory", "cc");
}

static inline void disk_wait(void)
{
    while ((inb(IO_PORT_BASE + OFFSET_STATUS) & (STATUS_READY | STATUS_BUSY)) != STATUS_READY)
        ;
}

static inline void disk_read_sector(byte *dst, u32 lba)
{
    disk_wait();
    outb(IO_PORT_BASE + OFFSET_LBA_EXTRA, ((lba >> 24) & 0x0f) | 0xe0);
    outb(IO_PORT_BASE + OFFSET_SECTOR_COUNT, 1);
    outb(IO_PORT_BASE + OFFSET_LBA_LOW, lba);
    outb(IO_PORT_BASE + OFFSET_LBA_MID, lba >> 8);
    outb(IO_PORT_BASE + OFFSET_LBA_HIGH, lba >> 16);
    outb(IO_PORT_BASE + OFFSET_COMMAND, COMMAND_READ_PIO);

    disk_wait();
    insl(IO_PORT_BASE, dst, SECTOR_SIZE / 4);
}

static inline int disk_read(byte *dst, sz count, sz byte_offset, sz sector_offset)
{
    byte *end = dst + count;
    u32 lba = (byte_offset / SECTOR_SIZE) + sector_offset;

    dst -= byte_offset % SECTOR_SIZE;

    while (dst < end) {
        disk_read_sector(dst, lba);
        dst += SECTOR_SIZE;
        lba++;
    }

    return 0;
}

#define EI_NIDENT 16

struct elf64_hdr {
    unsigned char ident[16];
    u16 type;
    u16 machine;
    u16 version;
    ptr entry;
    u64 phdr_tab_offset;
    u64 shdr_tab_offset;
    u32 flags;
    u16 header_size;
    u16 phdr_size;
    u16 phdr_count;
    u16 shdr_size;
    u16 shdr_count;
    u16 str_tab_idx;
};

struct elf64_phdr {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    ptr paddr;
    u64 file_size;
    u64 mem_size;
    u64 align;
};

#define PT_LOAD 1

typedef void (*entry_func_t)(void);

#define OFFSET_LINE_STATUS 5
#define LINE_STATUS_TX_READY BIT(5)

int com_write(struct str str)
{
    if (!str.dat || str.len <= 0)
        return -1;

    while (str.len--) {
        while (!(inb(0x3f8 + OFFSET_LINE_STATUS) & LINE_STATUS_TX_READY))
            ;
        outb(0x3f8, *str.dat++);
    }

    return 0;
}

void load_kernel(void)
{
    struct elf64_hdr *elf;
    struct elf64_phdr *phdr_iter, *phdr_end;
    entry_func_t entry;
    byte *paddr;

    com_write(STR("Starting to parse ELF\n"));

    // Any unused space works
    elf = (struct elf64_hdr *)SCRATCH_SPACE;
    disk_read((byte *)elf, LOAD_COUNT, 0);

    phdr_iter = (struct elf64_phdr *)((byte *)elf + elf->phdr_tab_offset);
    phdr_end = phdr_iter + elf->phdr_count;

    while (phdr_iter < phdr_end) {
        if (phdr_iter->type != PT_LOAD)
            continue;
        paddr = (byte *)phdr_iter->paddr;
        disk_read(paddr, phdr_iter->file_size, phdr_iter->offset);
        if (phdr_iter->mem_size > phdr_iter->file_size)
            stosb(paddr + phdr_iter->file_size, 0, phdr_iter->mem_size - phdr_iter->file_size);
        phdr_iter++;
    }

    entry = (entry_func_t)elf->entry;
    entry();
}
