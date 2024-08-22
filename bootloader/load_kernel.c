#include <tx/asm.h>
#include <tx/base.h>
#include <tx/fmt.h>
#include <tx/string.h>

#define ATA_IO_PORT_BASE 0x1f0
#define ATA_OFFSET_SECTOR_COUNT 2
#define ATA_OFFSET_LBA_LOW 3
#define ATA_OFFSET_LBA_MID 4
#define ATA_OFFSET_LBA_HIGH 5
#define ATA_OFFSET_LBA_EXTRA 6
#define ATA_OFFSET_STATUS 7
#define ATA_OFFSET_COMMAND 7

#define ATA_COMMAND_READ_PIO BIT(5)

#define ATA_STATUS_READY BIT(6)
#define ATA_STATUS_BUSY BIT(7)

#define SECTOR_SIZE 512

#define COM_OFFSET_LINE_STATUS 5
#define COM_LINE_STATUS_TX_READY BIT(5)
#define COM_PORT 0x3f8

__section(".elf_buf") static byte elf_buf[0x200];

int com_write(u16 port, struct str str)
{
    if (!str.dat || str.len <= 0)
        return -1;

    while (str.len--) {
        while (!(inb(port + COM_OFFSET_LINE_STATUS) & COM_LINE_STATUS_TX_READY))
            ;
        outb(port, *str.dat++);
    }

    return 0;
}

int print_str(struct str str)
{
    return com_write(COM_PORT, str);
}

int print_fmt(struct str_buf buf, struct str fmt, ...)
{
    va_list argp;
    int rc;
    va_start(argp, fmt);
    rc = vfmt(&buf, fmt, argp);
    if (rc < 0)
        return rc;
    rc = com_write(COM_PORT, str_from_buf(buf));
    va_end(argp);
    return rc;
}

static inline void disk_wait(void)
{
    while ((inb(ATA_IO_PORT_BASE + ATA_OFFSET_STATUS) & (ATA_STATUS_READY | ATA_STATUS_BUSY)) != ATA_STATUS_READY)
        ;
}

static inline void disk_read_sector(byte *dst, u32 lba)
{
    disk_wait();
    outb(ATA_IO_PORT_BASE + ATA_OFFSET_LBA_EXTRA, ((lba >> 24) & 0x0f) | 0xe0);
    outb(ATA_IO_PORT_BASE + ATA_OFFSET_SECTOR_COUNT, 1);
    outb(ATA_IO_PORT_BASE + ATA_OFFSET_LBA_LOW, lba);
    outb(ATA_IO_PORT_BASE + ATA_OFFSET_LBA_MID, lba >> 8);
    outb(ATA_IO_PORT_BASE + ATA_OFFSET_LBA_HIGH, lba >> 16);
    outb(ATA_IO_PORT_BASE + ATA_OFFSET_COMMAND, ATA_COMMAND_READ_PIO);

    disk_wait();
    insl(ATA_IO_PORT_BASE, dst, SECTOR_SIZE / 4);
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
    unsigned char ident[EI_NIDENT];
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
#define ET_EXEC 2
#define EM_X86_64 62

int elf_verify(struct elf64_hdr *elf)
{
    if (!(elf->ident[0] == 0x7f && elf->ident[1] == 'E' && elf->ident[2] == 'L' && elf->ident[3] == 'F'))
        return -1;

    if (elf->header_size != sizeof(struct elf64_hdr))
        return -1;

    if (elf->phdr_size != sizeof(struct elf64_phdr))
        return -1;

    if (elf->type != ET_EXEC)
        return -1;

    if (elf->machine != EM_X86_64)
        return -1;

    return 0;
}

typedef void (*entry_func_t)(void);

void load_kernel(void)
{
    struct elf64_hdr *elf;
    struct elf64_phdr *phdr_iter, *phdr_end;
    entry_func_t entry;
    byte *paddr;
    sz n_load;
    char underlying[1024];
    struct str_buf buf = str_buf_new(underlying, 0, countof(underlying));

    print_str(STR("Loading kernel ELF\n"));
    elf = (struct elf64_hdr *)elf_buf;
    disk_read((byte *)elf, sizeof(struct elf64_hdr), 0, BOOT_SECTOR_COUNT);

    if (elf_verify(elf) < 0) {
        print_str(STR("Failed to verify ELF\n"));
        return;
    }

    print_fmt(buf, STR("Loading program headers: phdr_tab_offset=0x%lx phdr_count=%hu phdr_size=%hu\n"),
              elf->phdr_tab_offset, elf->phdr_count, elf->phdr_size);

    n_load = elf->phdr_tab_offset + elf->phdr_size * elf->phdr_count;
    if (n_load > countof(elf_buf)) {
        print_fmt(buf, STR("ELF buf is not big enough to load %lu bytes\n"), n_load);
        return;
    }
    disk_read((byte *)elf, n_load, 0, BOOT_SECTOR_COUNT);

    phdr_iter = (struct elf64_phdr *)((byte *)elf + elf->phdr_tab_offset);
    phdr_end = phdr_iter + elf->phdr_count;

    for (; phdr_iter < phdr_end; phdr_iter++) {
        if (phdr_iter->type != PT_LOAD)
            continue;
        print_fmt(buf, STR("Loading segment: paddr=0x%lx file_size=0x%lx mem_size=0x%lx offset=0x%lx\n"),
                  phdr_iter->paddr, phdr_iter->file_size, phdr_iter->mem_size, phdr_iter->offset);
        paddr = (byte *)phdr_iter->paddr;
        disk_read(paddr, phdr_iter->file_size, phdr_iter->offset, BOOT_SECTOR_COUNT);
        if (phdr_iter->mem_size > phdr_iter->file_size)
            stosb(paddr + phdr_iter->file_size, 0, phdr_iter->mem_size - phdr_iter->file_size);
    }

    entry = (entry_func_t)elf->entry;
    print_fmt(buf, STR("Calling entry: 0x%lx\n"), entry);
    entry();
}
