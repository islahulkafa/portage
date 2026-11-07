#pragma once

#include <cstdint>

namespace portage::elf {

using Elf64_Addr = uint64_t;
using Elf64_Off = uint64_t;
using Elf64_Half = uint16_t;
using Elf64_Word = uint32_t;
using Elf64_Sword = int32_t;
using Elf64_Xword = uint64_t;
using Elf64_Sxword = int64_t;

constexpr unsigned char EI_MAG0 = 0;
constexpr unsigned char EI_MAG1 = 1;
constexpr unsigned char EI_MAG2 = 2;
constexpr unsigned char EI_MAG3 = 3;
constexpr unsigned char EI_CLASS = 4;
constexpr unsigned char EI_DATA = 5;
constexpr unsigned char EI_VERSION = 6;
constexpr unsigned char EI_OSABI = 7;
constexpr unsigned char EI_ABIVERSION = 8;
constexpr unsigned char EI_NIDENT = 16;

constexpr unsigned char ELFMAG0 = 0x7f;
constexpr unsigned char ELFMAG1 = 'E';
constexpr unsigned char ELFMAG2 = 'L';
constexpr unsigned char ELFMAG3 = 'F';

constexpr unsigned char ELFCLASS64 = 2;
constexpr unsigned char ELFDATA2LSB = 1;

constexpr Elf64_Half ET_EXEC = 2;
constexpr Elf64_Half ET_DYN = 3;

constexpr Elf64_Half EM_X86_64 = 62;

constexpr Elf64_Word PT_NULL = 0;
constexpr Elf64_Word PT_LOAD = 1;
constexpr Elf64_Word PT_DYNAMIC = 2;
constexpr Elf64_Word PT_INTERP = 3;
constexpr Elf64_Word PT_NOTE = 4;
constexpr Elf64_Word PT_SHLIB = 5;
constexpr Elf64_Word PT_PHDR = 6;
constexpr Elf64_Word PT_TLS = 7;
constexpr Elf64_Word PT_GNU_EH_FRAME = 0x6474e550;
constexpr Elf64_Word PT_GNU_STACK = 0x6474e551;
constexpr Elf64_Word PT_GNU_RELRO = 0x6474e552;

constexpr Elf64_Word PF_X = 1 << 0;
constexpr Elf64_Word PF_W = 1 << 1;
constexpr Elf64_Word PF_R = 1 << 2;

constexpr uint64_t AT_NULL = 0;
constexpr uint64_t AT_IGNORE = 1;
constexpr uint64_t AT_EXECFD = 2;
constexpr uint64_t AT_PHDR = 3;
constexpr uint64_t AT_PHENT = 4;
constexpr uint64_t AT_PHNUM = 5;
constexpr uint64_t AT_PAGESZ = 6;
constexpr uint64_t AT_BASE = 7;
constexpr uint64_t AT_FLAGS = 8;
constexpr uint64_t AT_ENTRY = 9;
constexpr uint64_t AT_NOTELF = 10;
constexpr uint64_t AT_UID = 11;
constexpr uint64_t AT_EUID = 12;
constexpr uint64_t AT_GID = 13;
constexpr uint64_t AT_EGID = 14;
constexpr uint64_t AT_PLATFORM = 15;
constexpr uint64_t AT_HWCAP = 16;
constexpr uint64_t AT_CLKTCK = 17;
constexpr uint64_t AT_SECURE = 23;
constexpr uint64_t AT_BASE_PLATFORM = 24;
constexpr uint64_t AT_RANDOM = 25;
constexpr uint64_t AT_HWCAP2 = 26;
constexpr uint64_t AT_EXECFN = 31;
constexpr uint64_t AT_SYSINFO_EHDR = 33;

struct Elf64_Ehdr {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
};

struct Elf64_Phdr {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
};

constexpr Elf64_Word SHT_NULL = 0;
constexpr Elf64_Word SHT_PROGBITS = 1;
constexpr Elf64_Word SHT_SYMTAB = 2;
constexpr Elf64_Word SHT_STRTAB = 3;
constexpr Elf64_Word SHT_DYNSYM = 11;

struct Elf64_Shdr {
    Elf64_Word sh_name;
    Elf64_Word sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr sh_addr;
    Elf64_Off sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word sh_link;
    Elf64_Word sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
};

struct Elf64_Sym {
    Elf64_Word st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half st_shndx;
    Elf64_Addr st_value;
    Elf64_Xword st_size;
};

struct Elf64_auxv_t {
    uint64_t a_type;
    union {
        uint64_t a_val;
    } a_un;
};

constexpr int64_t DT_NULL = 0;
constexpr int64_t DT_NEEDED = 1;
constexpr int64_t DT_PLTRELSZ = 2;
constexpr int64_t DT_HASH = 4;
constexpr int64_t DT_STRTAB = 5;
constexpr int64_t DT_SYMTAB = 6;
constexpr int64_t DT_RELA = 7;
constexpr int64_t DT_RELASZ = 8;
constexpr int64_t DT_RELAENT = 9;
constexpr int64_t DT_STRSZ = 10;
constexpr int64_t DT_INIT = 12;
constexpr int64_t DT_FINI = 13;
constexpr int64_t DT_SONAME = 14;
constexpr int64_t DT_RPATH = 15;
constexpr int64_t DT_JMPREL = 23;
constexpr int64_t DT_INIT_ARRAY = 25;
constexpr int64_t DT_FINI_ARRAY = 26;
constexpr int64_t DT_INIT_ARRAYSZ = 27;
constexpr int64_t DT_FINI_ARRAYSZ = 28;
constexpr int64_t DT_RUNPATH = 29;
constexpr int64_t DT_FLAGS = 30;

constexpr int RTLD_LAZY = 1;
constexpr int RTLD_NOW = 2;
constexpr int RTLD_LOCAL = 0;
constexpr int RTLD_GLOBAL = 0x00100;
inline void *const RTLD_DEFAULT = reinterpret_cast<void *>(0);
inline void *const RTLD_NEXT = reinterpret_cast<void *>(-1);

struct Dl_info {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
};

constexpr uint32_t R_X86_64_NONE = 0;
constexpr uint32_t R_X86_64_64 = 1;
constexpr uint32_t R_X86_64_GLOB_DAT = 6;
constexpr uint32_t R_X86_64_JUMP_SLOT = 7;
constexpr uint32_t R_X86_64_RELATIVE = 8;

struct Elf64_Dyn {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr d_ptr;
    } d_un;
};

struct Elf64_Rela {
    Elf64_Addr r_offset;
    Elf64_Xword r_info;
    Elf64_Sxword r_addend;
};

inline uint32_t ELF64_R_SYM(uint64_t i) { return static_cast<uint32_t>(i >> 32); }
inline uint32_t ELF64_R_TYPE(uint64_t i) { return static_cast<uint32_t>(i & 0xffffffff); }

} // namespace portage::elf
