#include "symbols.h"
#include "elf.h"
#include "debug.h"
#include "str.h"
#include <memory/early_alloc.h>


// 管理内核符号表


typedef struct symbol {
    size_t      addr;
    size_t      size;
    const char *name;
} symbol_t;

static CONST int         g_sym_num = 0;
static CONST symbol_t   *g_syms    = NULL;


// 遍历每个符号表两次，第一次统计符号数量和符号名长度，第二次备份符号数据
// TODO 还可以处理 debug section，识别 dwarf 调试信息
INIT_TEXT void parse_kernel_symtab(void *ptr, uint32_t entsize, unsigned num) {
    if (sizeof(Elf64_Shdr) != entsize) {
        log("section entry size %d\n", entsize);
        return;
    }

    const Elf64_Shdr *secs = (const Elf64_Shdr *)ptr;

    g_sym_num = 0;
    size_t strbuf_len = 0; // 符号名字符串总长

    for (unsigned i = 0; i < num; ++i) {
        const Elf64_Shdr *sec = &secs[i];
        if ((SHT_SYMTAB != sec->sh_type) && (SHT_DYNSYM != sec->sh_type)) {
            continue;
        }
        if ((SHN_UNDEF == sec->sh_link) || (sec->sh_link >= num)) {
            continue;
        }

        const Elf64_Shdr *link = &secs[sec->sh_link];
        if (SHT_STRTAB != link->sh_type) {
            continue;
        }

        size_t sym_num = sec->sh_size / sec->sh_entsize;
        const Elf64_Sym *symtab = (const Elf64_Sym *)sec->sh_addr;
        const char *strtab = (const char *)link->sh_addr;

        // log("%d symbols, strtab length %d\n", sym_num, strtab_size);

        // 遍历该表中的每个符号，只记录函数类型的符号
        for (size_t j = 1; j < sym_num; ++j) {
            const Elf64_Sym *sym = &symtab[j];
            if (STT_FUNC != ELF64_ST_TYPE(sym->st_info)) {
                continue;
            }

            const char *name = &strtab[sym->st_name];
            if ('\0' == name[0]) {
                continue;
            }

            strbuf_len += strlen(name) + 1; // 包含字符串末尾的 '\0'
            ++g_sym_num;
        }
    }

    // 为符号表和符号名字符串表分配空间
    g_syms = early_alloc_ro(g_sym_num * sizeof(symbol_t));
    char *str_buf = early_alloc_ro(strbuf_len);
    int sym_idx = 0;
    // int str_idx = 0;
    char *str_ptr = str_buf;

    for (unsigned i = 0; i < num; ++i) {
        const Elf64_Shdr *sec = &secs[i];
        if ((SHT_SYMTAB != sec->sh_type) && (SHT_DYNSYM != sec->sh_type)) {
            continue;
        }
        if ((SHN_UNDEF == sec->sh_link) || (sec->sh_link >= num)) {
            continue;
        }

        const Elf64_Shdr *link = &secs[sec->sh_link];
        if (SHT_STRTAB != link->sh_type) {
            continue;
        }

        size_t sym_num = sec->sh_size / sec->sh_entsize;
        const Elf64_Sym *symtab = (const Elf64_Sym *)sec->sh_addr;
        const char *strtab = (const char *)link->sh_addr;

        for (size_t j = 1; j < sym_num; ++j) {
            const Elf64_Sym *sym = &symtab[j];
            if (STT_FUNC != ELF64_ST_TYPE(sym->st_info)) {
                continue;
            }

            const char *name = &strtab[sym->st_name];
            if ('\0' == name[0]) {
                continue;
            }

            g_syms[sym_idx].addr = sym->st_value;
            g_syms[sym_idx].size = sym->st_size;
            g_syms[sym_idx].name = str_ptr;

            size_t copysize = strlen(name) + 1;
            memcpy(str_ptr, name, copysize);

            ++sym_idx;
            str_ptr += copysize;
        }
    }

    ASSERT(sym_idx == g_sym_num);
    ASSERT(str_ptr == str_buf + strbuf_len);
}

// size_t sym_locate(const char *name) {
//     //
// }

// const char *sym_resolve(size_t addr, size_t *rela) {
//     //
// }

void dump_symbols() {
    for (int i = 0; i < g_sym_num; ++i) {
        log("addr: %016lx, sym: %s\n", g_syms[i].addr, g_syms[i].name);
    }
}
