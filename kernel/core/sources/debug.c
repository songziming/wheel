#include <debug.h>
#include <arch_api.h>
#include <libk_string.h>
#include <libk_format.h>
#include <libk_elf.h>


//------------------------------------------------------------------------------
// 调试输出
//------------------------------------------------------------------------------

dbg_print_func_t g_dbg_print_func = NULL;

static void print_cb(void *para, const char *s, size_t n) {
    (void)para;

    // TODO 将调试输出写入文件 /var/dbg

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


typedef struct elf_section {
    uint32_t    type;
    const char *name;
    void       *addr; // 指向虚拟地址
    size_t      size;
} elf_section_t;

typedef struct elf_symbol {
    size_t      addr;
    size_t      size;
    const char *name;
} elf_symbol_t;

static CONST int            g_section_num  = 0;
static CONST elf_section_t *g_sections     = NULL;

static CONST int            g_symbol_num = 0;
static CONST elf_symbol_t  *g_symbols    = NULL;


static const char *elf_sec_type(uint32_t type) {
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
    default:                return "unknown";       break;
    }
}

// TODO 仅备份符号表，还是连带 section 表、字符串表都复制一份？

// 解析 ELF sections，解析符号表
// ELF 文件可能有多个符号表
INIT_TEXT void symtab_init(void *ptr, uint32_t entsize, uint32_t num, uint32_t shndx) {
    ASSERT(NULL != ptr);
    ASSERT(num < INT_MAX);
    ASSERT(shndx < num);

    if (sizeof(Elf64_Shdr) != entsize) {
        dbg_print("section entry size is %d\n", entsize);
        return;
    }

    // 准备 section 表
    Elf64_Shdr *sections = (Elf64_Shdr *)ptr;
    g_section_num = (int)num;
    g_sections = early_alloc_ro(num * sizeof(elf_section_t));

    // 填充 section 表
    for (int i = 0; i < g_section_num; ++i) {
        Elf64_Shdr *sec = &sections[i];

        g_sections[i].type = sec->sh_type;
        if (SHT_NULL == sec->sh_type) {
            continue;
        }

        g_sections[i].size = sec->sh_size;

        // 如果是代码段或数据段，则段内容已在内存中
        if ((SHT_PROGBITS == sec->sh_type) || (SHT_NOBITS == sec->sh_type)) {
            g_sections[i].addr = (void *)sec->sh_addr;
        } else {
            g_sections[i].addr = early_alloc_ro(sec->sh_size);
            kmemcpy(g_sections[i].addr, (void *)sec->sh_addr, sec->sh_size);
        }
    }

    // 获取 section 名称的字符串表
    size_t name_len = 1;
    char *name_buf = "";
    if (SHN_UNDEF != shndx) {
        name_len = g_sections[shndx].size;
        name_buf = g_sections[shndx].addr;
    }

    // 再次遍历每个 section，设置 section 名称
    // 同时统计符号表的数量，符号的数量
    int symtab_num = 0;
    g_symbol_num = 0;
    for (int i = 0; i < g_section_num; ++i) {
        uint32_t name_idx = sections[i].sh_name;
        if (name_idx >= name_len) {
            name_idx = name_len - 1;
        }
        g_sections[i].name = &name_buf[name_idx];
#if DEBUG
        dbg_print(" -> section type %10s, addr %18p, size %016lx, name '%s'\n",
            elf_sec_type(g_sections[i].type), g_sections[i].addr,
            g_sections[i].size, g_sections[i].name);
#endif

        // 如果这是符号表，计算包含的符号个数
        if ((SHT_SYMTAB != g_sections[i].type) && (SHT_DYNSYM != g_sections[i].type)) {
            continue;
        }
        ASSERT(sizeof(Elf64_Sym) == sections[i].sh_entsize);
        ++symtab_num;
        g_symbol_num += sections[i].sh_size / sections[i].sh_entsize;
    }
    dbg_print("%d symtabs, and %d total symbols\n", symtab_num, g_symbol_num);

    // TODO 申请符号表空间（每个符号表第一个元素都是 UNDEF）
    g_symbols = early_alloc_ro(g_symbol_num * sizeof(elf_symbol_t));

    // 再次遍历 section，解析符号表
    for (int i = 0; i < g_section_num; ++i) {
        if ((SHT_SYMTAB != g_sections[i].type) && (SHT_DYNSYM != g_sections[i].type)) {
            continue;
        }
        int sym_num = sections[i].sh_size / sections[i].sh_entsize;
        Elf64_Sym *syms = (Elf64_Sym *)g_sections[i].addr;

        // 该符号表对应的字符串表
        uint32_t stridx = sections[i].sh_link;
        Elf64_Shdr *shstr = &sections[stridx];
        ASSERT(SHT_STRTAB == shstr->sh_type);
        size_t symstr_len = shstr->sh_size;
        const char *symstr_buf = (const char *)shstr->sh_addr;

        // 遍历该表中的每个符号，只记录函数类型的符号
        int func_num = 0;
        for (int j = 0; j < sym_num; ++j) {
            if (STT_FUNC != ELF64_ST_TYPE(syms[j].st_info)) {
                continue;
            }
            ++func_num;

            uint32_t name_idx = syms[j].st_name;
            if (name_idx >= symstr_len) {
                name_idx = symstr_len - 1;
            }
            dbg_print("function %s\n", &symstr_buf[name_idx]);
        }
        dbg_print("%d function symbols in this table\n", func_num);
    }
}
