#include <debug.h>
#include <wheel.h>
#include <format.h>
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
        klog("warning: section entry size is %d\n", entsize);
        return;
    }

    // GRUB 传入的 elf 数据结构可能未按 8 字节对齐
    // 首要首先复制到局部变量，再通过局部变量访问成员字段
    const Elf64_Shdr *sections = (const Elf64_Shdr *)ptr;

    // 遍历 section，统计函数符号数量，符号名总长度
    g_symbol_num = 0;
    g_symlen_max = 0;
    size_t symname_len = 0;
    for (uint32_t i = 1; i < num; ++i) {
        Elf64_Shdr sh;
        memcpy(&sh, &sections[i], sizeof(Elf64_Shdr));
        if ((SHT_SYMTAB != sh.sh_type) && (SHT_DYNSYM != sh.sh_type)) {
            continue;
        }
        if ((SHN_UNDEF == sh.sh_link) || (sh.sh_link >= num)) {
            continue;
        }

        Elf64_Shdr link;
        memcpy(&link, &sections[sh.sh_link], sizeof(Elf64_Shdr));
        if (SHT_STRTAB != link.sh_type) {
            continue;
        }

        size_t symbol_num = sh.sh_size / sh.sh_entsize;
        Elf64_Sym *symbols = (Elf64_Sym *)sh.sh_addr;
        size_t strtab_len = link.sh_size;
        const char *strtab = (const char *)link.sh_addr;

        // 遍历该表中的每个符号，只记录函数类型的符号
        for (size_t j = 1; j < symbol_num; ++j) {
            Elf64_Sym st;
            memcpy(&st, &symbols[j], sizeof(Elf64_Sym));

            uint32_t strtab_idx = st.st_name;
            if (strtab_idx >= strtab_len) {
                continue;
            }
            const char *symstr = &strtab[strtab_idx];

            unsigned char symtype = ELF64_ST_TYPE(st.st_info);
            if (STT_FUNC != symtype) {
                continue;
            }

            ++g_symbol_num;
            size_t symlen = strnlen(symstr, strtab_len - strtab_idx) + 1;
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
        Elf64_Shdr sh;
        memcpy(&sh, &sections[i], sizeof(Elf64_Shdr));
        if ((SHT_SYMTAB != sh.sh_type) && (SHT_DYNSYM != sh.sh_type)) {
            continue;
        }
        if ((SHN_UNDEF == sh.sh_link) || (sh.sh_link >= num)) {
            continue;
        }

        Elf64_Shdr link;
        memcpy(&link, &sections[sh.sh_link], sizeof(Elf64_Shdr));
        if (SHT_STRTAB != link.sh_type) {
            continue;
        }

        size_t symbol_num = sh.sh_size / sh.sh_entsize;
        Elf64_Sym *symbols = (Elf64_Sym *)sh.sh_addr;
        size_t strtab_len = link.sh_size;
        const char *strtab = (const char *)link.sh_addr;

        for (size_t j = 1; j < symbol_num; ++j) {
            Elf64_Sym st;
            memcpy(&st, &symbols[j], sizeof(Elf64_Sym));

            uint32_t strtab_idx = st.st_name;
            if (strtab_idx >= strtab_len) {
                continue;
            }

            unsigned char symtype = ELF64_ST_TYPE(st.st_info);
            if (STT_FUNC != symtype) {
                continue;
            }

            char *symdst = &symname[symname_idx];
            const char *symstr = &strtab[strtab_idx];
            strncpy(symdst, symstr, symname_len - symname_idx);
            symname_idx += strnlen(symdst, symname_len - symname_idx) + 1;

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
        if (strncmp(name, g_symbols[i].name, g_symlen_max)) {
            continue;
        }
        return g_symbols[i].addr;
    }

    return INVALID_ADDR;
}

const char *sym_resolve(size_t addr, size_t *rela) {
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

void print_frames(const size_t *frames, int num) {
    for (int i = 0; i < num; ++i) {
        size_t rela;
        const char *name = sym_resolve(frames[i], &rela);
        klog(" -> frame %2d: %s + 0x%zx\n", i, name, rela);
    }
}

void print_stacktrace() {
    size_t frames[32];
    int depth = unwind(frames, 32);
    print_frames(&frames[1], depth - 1);
}


//------------------------------------------------------------------------------
// 处理断言失败
//------------------------------------------------------------------------------

void handle_assert_fail(const char *file, const char *func, int line) {
    klog("Assert failed %s:%d, function %s\n", file, line, func);
    print_stacktrace();
    // emu_exit(1);
    while (1) {
        cpu_halt();
    }
}



//------------------------------------------------------------------------------
// 处理栈溢出（-fstack-protector）
//------------------------------------------------------------------------------

const uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;

NORETURN void __stack_chk_fail() {
    klog("fatal: stack smashing detected\n");
    print_stacktrace();
    emu_exit(1);
}
