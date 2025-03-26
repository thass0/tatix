#ifndef __TX_ELF64_H__
#define __TX_ELF64_H__

#include <tx/base.h>

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

static inline bool elf64_is_valid(struct elf64_hdr *elf)
{
    return elf->ident[0] == 0x7f && elf->ident[1] == 'E' && elf->ident[2] == 'L' && elf->ident[3] == 'F' &&
           elf->header_size == sizeof(struct elf64_hdr) && elf->phdr_size == sizeof(struct elf64_phdr) &&
           elf->type == ET_EXEC && elf->machine == EM_X86_64;
}

#endif // __TX_ELF64_H__
