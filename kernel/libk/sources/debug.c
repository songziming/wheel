#include <debug.h>
#include <strlib.h>
#include <format.h>
#include <elf.h>


// 启动阶段是否打印内核 section、symbol 信息
#define SHOW_ALL_SECTIONS 1
#define SHOW_ALL_SYMBOLS  0


//------------------------------------------------------------------------------
// 调试输出
//------------------------------------------------------------------------------

static log_func_t g_log_func = NULL;

void set_log_func(log_func_t func) {
    g_log_func = func;
}

static void print_cb(void *para, const char *s, size_t n) {
    (void)para;

    // TODO 启动阶段，准备一段 buffer 作为临时文件缓冲区（ringbuf）
    //      临时 buffer 满了就覆盖最早写入的内容
    // TODO 文件系统启动之后，将调试输出写入文件磁盘
    // TODO 提供虚拟文件 /var/dbg，包括磁盘上和 ringbuf 中的内容

    if (NULL != g_log_func) {
        g_log_func(s, n);
    }
}

// 打印调试输出
void klog(const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), print_cb, NULL, fmt, args);
    va_end(args);
}

// 当内核发生无法恢复的严重错误时调用此函数
// 打印相关信息到屏幕和文件，然后停机（不是关机）
// 如果在虚拟环境下，则设置返回值并关闭虚拟机
// void panic(const char *fmt, ...) {
// }


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
        klog("section entry size is %d\n", entsize);
        return;
    }

    const Elf64_Shdr *sections = (const Elf64_Shdr *)ptr;

#if defined(DEBUG) && SHOW_ALL_SECTIONS
    // 获取 section 名称的字符串表
    size_t shstrtab_len = 1;
    const char *shstrtab_buf = "";
    if ((SHN_UNDEF != shndx) && (shndx < num)) {
        shstrtab_len = sections[shndx].sh_size;
        shstrtab_buf = (const char *)sections[shndx].sh_addr;
    }
#endif

    // 遍历 section，统计函数符号数量，符号名总长度
    g_symbol_num = 0;
    int symname_len = 0;
    for (uint32_t i = 1; i < num; ++i) {
        uint32_t sectype = sections[i].sh_type;
        uint32_t seclink = sections[i].sh_link;

#if defined(DEBUG) && SHOW_ALL_SECTIONS
        uint32_t name_idx = sections[i].sh_name;
        if (name_idx >= shstrtab_len) {
            name_idx = shstrtab_len - 1;
        }
        klog("=> section type %10s, addr=%016lx, size=%08lx, flags=%02lx, name='%s'\n",
            elf_sec_type(sectype), sections[i].sh_addr,
            sections[i].sh_size, sections[i].sh_flags,
            &shstrtab_buf[name_idx]);
#endif

        // 带有字符串的符号表才需要处理
        int is_symtab = ((SHT_SYMTAB == sectype) || (SHT_DYNSYM == sectype)) &&
            (SHN_UNDEF != seclink) && (seclink < num) &&
            (SHT_STRTAB == sections[seclink].sh_type);
        if (!is_symtab) {
            continue;
        }

        size_t symbol_num = sections[i].sh_size / sections[i].sh_entsize;
        Elf64_Sym *symbols = (Elf64_Sym *)sections[i].sh_addr;
        size_t strtab_len = sections[seclink].sh_size;
        const char *strtab = (const char *)sections[seclink].sh_addr;

        // 遍历该表中的每个符号，只记录函数类型的符号
        for (size_t j = 1; j < symbol_num; ++j) {
            uint32_t strtab_idx = symbols[j].st_name;
            if (strtab_idx >= strtab_len) {
                continue;
            }
            const char *symstr = &strtab[strtab_idx];

            unsigned char symtype = ELF64_ST_TYPE(symbols[j].st_info);
#if defined(DEBUG) && SHOW_ALL_SYMBOLS
            klog("  - symtype %6s, addr=%016lx, size=%08lx, name='%s'\n",
                elf_sym_type(symtype), symbols[j].st_value,
                symbols[j].st_size, symstr);
#endif
            if (STT_FUNC != symtype) {
                continue;
            }

            ++g_symbol_num;
            symname_len += slen(symstr, strtab_len - strtab_idx) + 1;
        }
    }

    // 申请函数符号表和符号名称的空间
    int symname_idx = 0;
    char *symname = early_alloc_ro(symname_len);
    int symbol_idx = 0;
    g_symbols = early_alloc_ro(g_symbol_num * sizeof(elf_symbol_t));

    // 再次遍历符号表和符号，将函数类型的符号保存下来
    for (uint32_t i = 1; i < num; ++i) {
        uint32_t sectype = sections[i].sh_type;
        uint32_t seclink = sections[i].sh_link;
        int is_symtab = ((SHT_SYMTAB == sectype) || (SHT_DYNSYM == sectype)) &&
            (SHN_UNDEF != seclink) && (seclink < num) &&
            (SHT_STRTAB == sections[seclink].sh_type);
        if (!is_symtab) {
            continue;
        }

        size_t symbol_num = sections[i].sh_size / sections[i].sh_entsize;
        Elf64_Sym *symbols = (Elf64_Sym *)sections[i].sh_addr;
        size_t strtab_len = sections[seclink].sh_size;
        const char *strtab = (const char *)sections[seclink].sh_addr;

        for (size_t j = 1; j < symbol_num; ++j) {
            uint32_t strtab_idx = symbols[j].st_name;
            if (strtab_idx >= strtab_len) {
                continue;
            }

            unsigned char symtype = ELF64_ST_TYPE(symbols[j].st_info);
            if (STT_FUNC != symtype) {
                continue;
            }

            char *symdst = &symname[symname_idx];
            const char *symstr = &strtab[strtab_idx];
            scopy(symdst, symstr, symname_len - symname_idx);
            symname_idx += slen(symdst, symname_len - symname_idx) + 1;

            elf_symbol_t *sym = &g_symbols[symbol_idx++];
            sym->name = symdst;
            sym->addr = symbols[j].st_value;
            sym->size = symbols[j].st_size;
        }
    }

    ASSERT(symname_idx == symname_len);
    ASSERT(symbol_idx == g_symbol_num);
}

#ifdef DEBUG

INIT_TEXT void symtab_show() {
    ASSERT(g_symbol_num > 0);
    ASSERT(NULL != g_symbols);

    klog("kernel symbols:\n");
    for (int i = 0; i < g_symbol_num; ++i) {
        klog("  - addr=%016lx, size=%08lx, name='%s'\n",
            g_symbols[i].addr, g_symbols[i].size, g_symbols[i].name);
    }
}

#endif // DEBUG
