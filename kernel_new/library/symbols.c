#include "symbols.h"
#include "elf.h"
#include "debug.h"
#include "string.h"
#include <memory/early_alloc.h>

#define PARSE_DWARF

#ifdef PARSE_DWARF
#include "dwarf.h"
#endif

// 管理内核符号表，以及调试信息


typedef struct symbol {
    size_t      addr;
    size_t      size;
    const char *name;
} symbol_t;

static CONST int       g_sym_num = 0;
static CONST symbol_t *g_syms    = NULL;


// 遍历每个符号表两次，第一次统计符号数量和符号名长度，第二次备份符号数据
// TODO 还可以处理 debug section，识别 dwarf 调试信息
//      查看符号时可以精确到行号
INIT_TEXT void parse_kernel_symtab(void *ptr, uint32_t entsize, unsigned num, unsigned shstrndx UNUSED) {
    if (sizeof(Elf64_Shdr) != entsize) {
        log("section entry size %d\n", entsize);
        return;
    }

    const Elf64_Shdr *secs = (const Elf64_Shdr*)ptr;

#ifdef PARSE_DWARF
    // 段名字符串，匹配 debug 信息需要
    const char *shname = NULL;
    if ((SHN_UNDEF != shstrndx) && (shstrndx < num)) {
        const Elf64_Shdr shstr = secs[shstrndx];
        if (SHT_STRTAB == shstr.sh_type) {
            shname = (const char*)shstr.sh_addr;
        }
    }

    // 记录调试信息所在 section 的编号，0 表示未找到
    dwarf_line_t dbg_line = {0};
#endif // PARSE_DWARF

    g_sym_num = 0;
    size_t strbuf_len = 0; // 符号名字符串总长

    for (unsigned i = 0; i < num; ++i) {
        const Elf64_Shdr sec = secs[i];

#ifdef PARSE_DWARF
        if (NULL != shname) {
            const char *name = &shname[sec.sh_name];
            if (0 == strcmp(".debug_str", name)) {
                dbg_line.str = (const char*)sec.sh_addr;
                dbg_line.str_size = sec.sh_size;
            }
            if (0 == strcmp(".debug_line_str", name)) {
                dbg_line.line_str = (const char*)sec.sh_addr;
                dbg_line.line_str_size = sec.sh_size;
            }
            if (0 == strcmp(".debug_line", name)) {
                dbg_line.line = (uint8_t*)sec.sh_addr;
                dbg_line.line_end = dbg_line.line + sec.sh_size;
            }
        }
#endif // PARSE_DWARF

        if ((SHT_SYMTAB != sec.sh_type) && (SHT_DYNSYM != sec.sh_type)) {
            continue;
        }
        if ((SHN_UNDEF == sec.sh_link) || (sec.sh_link >= num)) {
            continue;
        }

        const Elf64_Shdr link = secs[sec.sh_link];
        if (SHT_STRTAB != link.sh_type) {
            continue;
        }

        size_t sym_num = sec.sh_size / sec.sh_entsize;
        const Elf64_Sym *symtab = (const Elf64_Sym*)sec.sh_addr;
        const char *strtab = (const char*)link.sh_addr;

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

#ifdef PARSE_DWARF
    // 如果找到了调试信息，则解析行号映射信息
    parse_debug_line(&dbg_line);
#endif

    // 为符号表和符号名字符串表分配空间
    g_syms = early_alloc_ro(g_sym_num * sizeof(symbol_t));
    char *str_buf = early_alloc_ro(strbuf_len);
    int sym_idx = 0;
    char *str_ptr = str_buf;

    // 不应直接访问内存里的 ELF 符号表，可能未对齐
    for (unsigned i = 0; i < num; ++i) {
        const Elf64_Shdr sec = secs[i];
        if ((SHT_SYMTAB != sec.sh_type) && (SHT_DYNSYM != sec.sh_type)) {
            continue;
        }
        if ((SHN_UNDEF == sec.sh_link) || (sec.sh_link >= num)) {
            continue;
        }

        const Elf64_Shdr link = secs[sec.sh_link];
        if (SHT_STRTAB != link.sh_type) {
            continue;
        }

        size_t sym_num = sec.sh_size / sec.sh_entsize;
        const Elf64_Sym *symtab = (const Elf64_Sym*)sec.sh_addr;
        const char *strtab = (const char*)link.sh_addr;

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

size_t sym_locate(const char *name) {
    for (int i = 0; i < g_sym_num; ++i) {
        if (strcmp(name, g_syms[i].name)) {
            continue;
        }
        return g_syms[i].addr;
    }

    return 0;
}

const char *sym_resolve(size_t addr, size_t *rela) {
    for (int i = 0; i < g_sym_num; ++i) {
        size_t start = g_syms[i].addr;
        size_t end = start + g_syms[i].size;
        if ((addr < start) || (addr >= end)) {
            continue;
        }
        if (rela) {
            *rela = addr - start;
        }
        return g_syms[i].name;
    }

    if (rela) {
        *rela = addr;
    }
    return NULL;
}

// void dump_symbols() {
//     for (int i = 0; i < g_sym_num; ++i) {
//         log("addr: %016lx, sym: %s\n", g_syms[i].addr, g_syms[i].name);
//     }
// }
