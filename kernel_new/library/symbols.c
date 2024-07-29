#include "symbols.h"
#include "elf.h"
#include <debug.h>


// 管理内核符号表


typedef struct symbol {
    size_t      addr;
    size_t      size;
    const char *name;
} symbol_t;

static CONST int       g_sym_num = 0;
static CONST symbol_t *g_syms = NULL;


// TODO 还可以处理 debug section，识别 dwarf 调试信息
INIT_TEXT void parse_kernel_symtab(void *ptr, uint32_t entsize, int num) {
    if (sizeof(Elf64_Shdr) != entsize) {
        log("section entry size %d\n", entsize);
        return;
    }

    const Elf64_Shdr *secs = (const Elf64_Shdr *)ptr;

    for (int i = 0; i < num; ++i) {
        Elf64_Shdr *sec = &secs[i];
        if ((SHT_SYMTAB != sec->sh_type) && (SHT_DYNSYM != sec->sh_type)) {
            continue;
        }
        if ((SHN_UNDEF == sec->sh_link) || (sec->sh_link >= num)) {
            continue;
        }

        Elf64_Shdr *link = &secs[sec->sh_link];
        if (SHT_STRTAB != link->sh_type) {
            continue;
        }

        size_t sym_num = sec->sh_size / sec->sh_entsize;
        Elf64_Sym *syms = (Elf64_Sym *)sec->sh_addr;
        size_t strtab_len = link->sh_size;
        const char *strtab = (const char *)link->sh_addr;

        log("%d symbols, strtab length %d\n", sym_num, strtab_len);
    }
}

// size_t sym_locate(const char *name) {
//     //
// }

// const char *sym_resolve(size_t addr, size_t *rela) {
//     //
// }
