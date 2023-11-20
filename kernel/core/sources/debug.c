#include <debug.h>
#include <wheel.h>
#include <format.h>
#include <str.h>
#include <elf.h>


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

// TODO 增加多种 LOG 级别，区分 FATAL、WARNING、DEBUG
void klog(const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), print_cb, NULL, fmt, args);
    va_end(args);
}

// TODO 严重错误时打印
// void panic() {}



//------------------------------------------------------------------------------
// 内核符号表
//------------------------------------------------------------------------------

typedef struct elf_symbol {
    size_t      addr;
    size_t      size;
    const char *name;
} elf_symbol_t;

static CONST int            g_symbol_num = 0;   // 符号个数
static CONST size_t         g_symlen_max = 0;   // 最长符号名长度（包含末尾的 0）
static CONST elf_symbol_t  *g_symbols    = NULL;

// 解析 ELF sections，解析符号表
INIT_TEXT void symtab_init(void *ptr, uint32_t entsize, uint32_t num) {
    ASSERT(0 == g_symbol_num);
    ASSERT(0 == g_symlen_max);
    ASSERT(NULL == g_symbols);
    ASSERT(NULL != ptr);

    if (sizeof(Elf64_Shdr) != entsize) {
        klog("section entry size is %d\n", entsize);
        return;
    }

    const Elf64_Shdr *sections = (const Elf64_Shdr *)ptr;

    // 遍历 section，统计函数符号数量，符号名总长度
    g_symbol_num = 0;
    g_symlen_max = 0;
    size_t symname_len = 0;
    for (uint32_t i = 1; i < num; ++i) {
        uint32_t sectype = sections[i].sh_type;
        uint32_t seclink = sections[i].sh_link;

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
            if (STT_FUNC != symtype) {
                continue;
            }

            ++g_symbol_num;
            size_t symlen = slen(symstr, strtab_len - strtab_idx) + 1;
            if (symlen > g_symlen_max) {
                g_symlen_max = symlen;
            }
            symname_len += symlen;
        }
    }

    // 申请函数符号表和符号名称的空间
    char *symname = early_alloc_ro(symname_len);
    g_symbols = early_alloc_ro(g_symbol_num * sizeof(elf_symbol_t));
    size_t symname_idx = 0;
    int symbol_idx = 0;

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
            scpy(symdst, symstr, symname_len - symname_idx);
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

size_t sym_locate(const char *name) {
    ASSERT(0 != g_symbol_num);
    ASSERT(0 != g_symlen_max);
    ASSERT(NULL != g_symbols);

    for (int i = 0; i < g_symbol_num; ++i) {
        if (scmp(name, g_symbols[i].name, g_symlen_max)) {
            continue;
        }
        return g_symbols[i].addr;
    }

    return INVALID_ADDR;
}

const char *sym_resolve(size_t addr, size_t *rela) {
    ASSERT(0 != g_symbol_num);
    ASSERT(0 != g_symlen_max);
    ASSERT(NULL != g_symbols);

    for (int i = 0; i < g_symbol_num; ++i) {
        size_t start = g_symbols[i].addr;
        size_t end = start + g_symbols[i].size;
        if ((addr < start) || (addr >= end)) {
            continue;
        }

        if (rela) {
            *rela = addr - start;
        }
        return g_symbols[i].name;
    }

    if (rela) {
        *rela = addr;
    }
    return "(null)";
}



//------------------------------------------------------------------------------
// 处理断言失败
//------------------------------------------------------------------------------

void handle_assert_fail(const char *file, const char *func, int line) {
    klog("Assert failed %s:%s:%d\n", file, func, line);

    size_t frames[32];
    int depth = unwind(frames, 32);
    for (int i = 1; i < depth; ++i) {
        size_t rela;
        const char *name = sym_resolve(frames[i], &rela);
        klog(" frame %2d: %s + 0x%zu\n", i, name, rela);
    }

    emu_exit(1);
}
