#include <tx/asm.h>
#include <tx/base.h>
#include <tx/string.h>

#define SCRATCH_SPACE 0x200000

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

    elf = (struct elf64_hdr *)SCRATCH_SPACE;
    disk_read((byte *)elf, sizeof(struct elf64_hdr), 0, BOOT_SECTOR_COUNT);

    if (elf_verify(elf) < 0)
        return;

    disk_read((byte *)elf, elf->phdr_tab_offset + elf->phdr_size * elf->phdr_count, 0, BOOT_SECTOR_COUNT);

    phdr_iter = (struct elf64_phdr *)((byte *)elf + elf->phdr_tab_offset);
    phdr_end = phdr_iter + elf->phdr_count;

    for (; phdr_iter < phdr_end; phdr_iter++) {
        if (phdr_iter->type != PT_LOAD)
            continue;
        paddr = (byte *)phdr_iter->paddr;
        disk_read(paddr, phdr_iter->file_size, phdr_iter->offset, BOOT_SECTOR_COUNT);
        if (phdr_iter->mem_size > phdr_iter->file_size)
            stosb(paddr + phdr_iter->file_size, 0, phdr_iter->mem_size - phdr_iter->file_size);
    }

    entry = (entry_func_t)elf->entry;
    entry();
}
