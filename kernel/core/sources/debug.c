#include <debug.h>
#include <arch_api.h>
#include <libk_string.h>
#include <libk_format.h>
#include <libk_elf.h>


// 启动阶段是否打印内核 section、symbol 信息
#define SHOW_ALL_SECTIONS 0
#define SHOW_ALL_SYMBOLS  0


//------------------------------------------------------------------------------
// 调试输出
//------------------------------------------------------------------------------

dbg_print_func_t g_dbg_print_func = NULL;

static void print_cb(void *para, const char *s, size_t n) {
    (void)para;

    // TODO 将调试输出写入文件 /var/dbg
    // TODO 启动阶段，准备一段 buffer 作为临时文件缓冲区（ringbuf）

    if (NULL != g_dbg_print_func) {
        g_dbg_print_func(s, n);
    }
}

// 打印调试输出
void dbg_print(const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), print_cb, NULL, fmt, args);
    va_end(args);
}

void report_assert_fail(const char *file, const char *func, int line) {
    dbg_print("Assert failed %s:%s:%d\n", file, func, line);
}


//------------------------------------------------------------------------------
// 内核符号表
//------------------------------------------------------------------------------

typedef struct elf_symbol {
    size_t      addr;
    size_t      size;
    const char *name;
} elf_symbol_t;

static CONST int            g_symbol_num = 0;
static CONST elf_symbol_t  *g_symbols    = NULL;


#ifdef DEBUG
#if SHOW_ALL_SECTIONS
static INIT_TEXT const char *elf_sec_type(uint32_t type) {
    switch (type) {
    case SHT_NULL:          return "NULL";          break;
    case SHT_PROGBITS:      return "PROGBITS";      break;
    case SHT_SYMTAB:        return "SYMTAB";        break;
    case SHT_STRTAB:        return "STRTAB";        break;
    case SHT_RELA:          return "RELA";          break;
    case SHT_HASH:          return "HASH";          break;
    case SHT_DYNAMIC:       return "DYNAMIC";       break;
    case SHT_NOTE:          return "NOTE";          break;
    case SHT_NOBITS:        return "NOBITS";        break;
    case SHT_REL:           return "REL";           break;
    case SHT_SHLIB:         return "SHLIB";         break;
    case SHT_DYNSYM:        return "DYNSYM";        break;
    case SHT_INIT_ARRAY:    return "INIT_ARRAY";    break;
    case SHT_FINI_ARRAY:    return "FINI_ARRAY";    break;
    case SHT_PREINIT_ARRAY: return "PREINIT_ARRAY"; break;
    case SHT_GROUP:         return "GROUP";         break;
    case SHT_SYMTAB_SHNDX:  return "SYMTAB_SHNDX";  break;
    default:                return "UNKNOWN";       break;
    }
}
#endif // SHOW_ALL_SECTIONS
#if SHOW_ALL_SYMBOLS
static INIT_TEXT const char *elf_sym_type(unsigned char type) {
    switch (type) {
    case STT_NOTYPE:  return "NOTYPE";  break;
    case STT_OBJECT:  return "OBJECT";  break;
    case STT_FUNC:    return "FUNC";    break;
    case STT_SECTION: return "SECTION"; break;
    case STT_FILE:    return "FILE";    break;
    case STT_COMMON:  return "COMMON";  break;
    case STT_TLS:     return "TLS";     break;
    default:          return "UNKNOWN"; break;
    }
}
#endif // SHOW_ALL_SYMBOLS
#endif // DEBUG


// 解析 ELF sections，解析符号表
INIT_TEXT void symtab_init(void *ptr, uint32_t entsize, uint32_t num, uint32_t shndx) {
    ASSERT(NULL != ptr);
    (void)shndx;

    if (sizeof(Elf64_Shdr) != entsize) {
        dbg_print("section entry size is %d\n", entsize);
        return;
    }

    const Elf64_Shdr *sections = (const Elf64_Shdr *)ptr;

#if defined(DEBUG) && SHOW_ALL_SECTIONS
    // 获取 section 名称的字符串表
    size_t secname_len = 1;
    const char *secname_buf = "";
    if ((SHN_UNDEF != shndx) && (shndx < num)) {
        secname_len = sections[shndx].sh_size;
        secname_buf = (const char *)sections[shndx].sh_addr;
    }
#endif

    // 遍历 section，统计函数符号数量，符号名总长度
    g_symbol_num = 0;
    int symstr_len = 0;
    for (uint32_t i = 1; i < num; ++i) {
        uint32_t type = sections[i].sh_type;
        uint32_t link = sections[i].sh_link;

#if defined(DEBUG) && SHOW_ALL_SECTIONS
        uint32_t name_idx = sections[i].sh_name;
        if (name_idx >= secname_len) {
            name_idx = secname_len - 1;
        }
        dbg_print("=> section type %10s, addr=%016lx, size=%08lx, flags=%02lx, name='%s'\n",
            elf_sec_type(type), sections[i].sh_addr,
            sections[i].sh_size, sections[i].sh_flags,
            &secname_buf[name_idx]);
#endif

        // 带有字符串的符号表才需要处理
        int is_symtab = ((SHT_SYMTAB == type) || (SHT_DYNSYM == type)) &&
            (SHN_UNDEF != link) && (link < num) &&
            (SHT_STRTAB == sections[link].sh_type);
        if (!is_symtab) {
            continue;
        }

        size_t symbol_num = sections[i].sh_size / sections[i].sh_entsize;
        Elf64_Sym *symbols = (Elf64_Sym *)sections[i].sh_addr;

        size_t symname_len = sections[link].sh_size;
        const char *symname_buf = (const char *)sections[link].sh_addr;

        // 遍历该表中的每个符号，只记录函数类型的符号
        for (size_t j = 1; j < symbol_num; ++j) {
            uint32_t name_idx = symbols[j].st_name;
            if (name_idx >= symname_len) {
                continue;
            }

            unsigned char type = ELF64_ST_TYPE(symbols[j].st_info);
#if defined(DEBUG) && SHOW_ALL_SYMBOLS
            dbg_print("  - symtype %6s, addr=%016lx, size=%08lx, name='%s'\n",
                elf_sym_type(type), symbols[j].st_value,
                symbols[j].st_size, &symname_buf[name_idx]);
#endif
            if (STT_FUNC != type) {
                continue;
            }

            ++g_symbol_num;
            symstr_len += kstrlen(&symname_buf[name_idx]) + 1;
        }
    }

    // 申请函数符号表和符号名称的空间
    char *symstr = early_alloc_ro(symstr_len);
    int symstr_idx = 0;
    g_symbols = early_alloc_ro(g_symbol_num * sizeof(elf_symbol_t));

    // 再次遍历符号表和符号，将函数类型的符号保存下来
    int symbol_idx = 0;
    for (uint32_t i = 1; i < num; ++i) {
        uint32_t type = sections[i].sh_type;
        uint32_t link = sections[i].sh_link;
        int is_symtab = ((SHT_SYMTAB == type) || (SHT_DYNSYM == type)) &&
            (SHN_UNDEF != link) && (link < num) &&
            (SHT_STRTAB == sections[link].sh_type);
        if (!is_symtab) {
            continue;
        }

        size_t symbol_num = sections[i].sh_size / sections[i].sh_entsize;
        Elf64_Sym *symbols = (Elf64_Sym *)sections[i].sh_addr;
        size_t symname_len = sections[link].sh_size;
        const char *symname_buf = (const char *)sections[link].sh_addr;

        for (size_t j = 1; j < symbol_num; ++j) {
            uint32_t name_idx = symbols[j].st_name;
            if (name_idx >= symname_len) {
                continue;
            }

            unsigned char type = ELF64_ST_TYPE(symbols[j].st_info);
            if (STT_FUNC != type) {
                continue;
            }

            char *name = &symstr[symstr_idx];
            kstrcpy(name, &symname_buf[name_idx]);
            symstr_idx += kstrlen(name) + 1;

            elf_symbol_t *sym = &g_symbols[symbol_idx++];
            sym->name = name;
            sym->addr = symbols[j].st_value;
            sym->size = symbols[j].st_size;
        }
    }

    ASSERT(symstr_idx == symstr_len);
    ASSERT(symbol_idx == g_symbol_num);
}

#ifdef DEBUG

INIT_TEXT void symtab_show() {
    ASSERT(g_symbol_num > 0);
    ASSERT(NULL != g_symbols);

    dbg_print("kernel symbols:\n");
    for (int i = 0; i < g_symbol_num; ++i) {
        dbg_print("  - addr=%016lx, size=%08lx, name='%s'\n",
            g_symbols[i].addr, g_symbols[i].size, g_symbols[i].name);
    }
}

#endif // DEBUG
