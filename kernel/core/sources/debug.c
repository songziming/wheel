#include <debug.h>
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

// 解析 ELF sections，解析符号表
// ELF 文件可能有多个符号表
INIT_TEXT void symtab_init(void *ptr, size_t num, size_t entsize, size_t shndx) {
    ASSERT(NULL != ptr);
    ASSERT(num > 0);
    ASSERT(sizeof(Elf64_Shdr) == entsize);
    ASSERT(shndx < num);

    Elf64_Shdr *sections = (Elf64_Shdr *)ptr;
    dbg_print("parsing %zd sections of size %zu, addr=%p, shndx=%zu\n",
            num, entsize, ptr, shndx);

    // 获取 section 名称的字符串表
    const char *secnamestr = "";
    size_t secnamelen = 1;
    if (SHN_UNDEF != shndx) {
        secnamestr = (const char *)sections[shndx].sh_addr;
        secnamelen = sections[shndx].sh_size;
        dbg_print("shndx type %s, size=%zd\n",
                elf_sec_type(sections[shndx].sh_type),
                secnamelen);
    }

    // 检查每个 section，第一个 section 一定是 NULL，跳过
    for (size_t i = 1; i < num; ++i) {
        uint32_t name = sections[i].sh_name;
        if (name >= secnamelen) {
            name = secnamelen - 1;
        }
        uint32_t type = sections[i].sh_type;
        dbg_print(" -> section type %10s, name %s\n",
                elf_sec_type(type),
                &secnamestr[name]);

        // 如果遇到未加载的段，将其复制到 early_ro，将数据保护起来
        if ((SHT_PROGBITS != type) && (SHT_NOBITS != type)) {
            //
        }
    }

    // 统计符号表的数量，符号数量
    int symtab_num = 0;
    int symbol_num = 0;
    for (size_t i = 0; i < num; ++i) {
        Elf64_Shdr *sh = &sections[i];
        if ((SHT_SYMTAB != sh->sh_type) && (SHT_DYNSYM != sh->sh_type)) {
            continue;
        }
        ASSERT(sizeof(Elf64_Sym) == sh->sh_entsize);
        ++symtab_num;
        symbol_num += sh->sh_size / sh->sh_entsize;
    }

    // TODO 申请符号表空间（每个符号表第一个元素都是 UNDEF）
    dbg_print("symbol num = %d\n", symbol_num - symtab_num);

    // TODO 再次遍历 sections，填充符号表
}
