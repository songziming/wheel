// 调试输出模块

#include <debug.h>
#include <arch_interface.h>
#include <libk.h>
#include <format.h>


//------------------------------------------------------------------------------
// 调试输出
//------------------------------------------------------------------------------

dbg_print_func_t g_print_func = NULL;

static void print_cb(void *para, const char *s, size_t n) {
    (void)para;

    // TODO 将字符串写入虚拟文件 /var/dbg

    if (NULL != g_print_func) {
        g_print_func(s, n);
    }
}

// 该函数负责格式化字符串
// 格式化结果记录在虚拟文件 /var/dbg
void dbg_print(const char *fmt, ...) {
    va_list args;
    char tmp[256];

    va_start(args, fmt);
    format(tmp, sizeof(tmp), print_cb, NULL, fmt, args);
    va_end(args);
}


//------------------------------------------------------------------------------
// 符号表
//------------------------------------------------------------------------------

typedef struct symbol {
    size_t      addr;
    size_t      size;
    const char *name;
} symbol_t;

static CONST int       g_symbol_num = 0;
static CONST symbol_t *g_symbols    = NULL;
static CONST char     *g_strtab     = NULL;


// 解析内核符号表，记录
INIT_TEXT void dbg_sym_init(Elf64_Shdr *secs, int n) {
    ASSERT(NULL != secs);
    ASSERT(n > 0);

    Elf64_Sym *sym_arr = NULL;
    size_t sym_num = 0;
    const char *sym_str = NULL;

    // TODO 可能有多个符号表 section
    for (int i = 0; i < n; ++i) {
        Elf64_Shdr *sh = &secs[i];

        if ((SHT_SYMTAB != sh->sh_type) && (SHT_DYNSYM != sh->sh_type)) {
            continue;
        }

        ASSERT(sizeof(Elf64_Sym) == sh->sh_entsize);
        sym_arr = (Elf64_Sym *)sh->sh_addr;
        sym_num = sh->sh_size / sh->sh_entsize;

        if (SHN_UNDEF != sh->sh_link) {
            Elf64_Shdr *shstr = &secs[sh->sh_link];
            ASSERT(SHT_STRTAB == shstr->sh_type);
            sym_str = (const char *)shstr->sh_addr;
        }
    }

    if ((NULL == sym_arr) || (0 == sym_num)) {
        return;
    }

    // 统计 FUNC 符号总数，符号字符串长度之和
    int func_num = 0;
    size_t str_size = 1;
    for (size_t i = 0; i < sym_num; ++i) {
        Elf64_Sym *sym = &sym_arr[i];
        if (STT_FUNC != ELF64_ST_TYPE(sym->st_info)) {
            continue;
        }
        ++func_num;
        size_t len = strlen(&sym_str[sym->st_name]);
        if (0 != len) {
            str_size += len + 1;
        }
    }

    // 申请符号表的空间
    g_symbol_num  = func_num;
    g_symbols = early_const_alloc(func_num * sizeof(symbol_t));
    g_strtab = early_const_alloc(str_size * sizeof(char));

    // 填充函数符号表内容
    func_num = 0;
    str_size = 1;
    g_strtab[0] = '\0';
    for (size_t i = 0; i < sym_num; ++i) {
        Elf64_Sym *sym = &sym_arr[i];
        if (STT_FUNC != ELF64_ST_TYPE(sym->st_info)) {
            continue;
        }

        g_symbols[func_num].addr = sym->st_value;
        g_symbols[func_num].size = sym->st_size;
        g_symbols[func_num].name = &g_strtab[0]; // 空串

        // st_size 为零表示符号大小不确定
        if (0 == sym->st_size) {
            g_symbols[func_num].size = UINT64_MAX - g_symbols[func_num].addr;
        }

        size_t len = strlen(&sym_str[sym->st_name]);
        if (0 != len) {
            g_symbols[func_num].name = &g_strtab[str_size];
            strcpy(&g_strtab[str_size], &sym_str[sym->st_name]);
            str_size += len + 1;
        }

        ++func_num;
    }

    // 对函数符号表排序，地址从小到大（冒泡）
    symbol_t tmp;
    for (int i = 0; i < func_num; ++i) {
        for (int j = 1; j < func_num - i; ++j) {
            if (g_symbols[j - 1].addr <= g_symbols[j].addr) {
                continue;
            }

            tmp = g_symbols[j - 1];
            g_symbols[j - 1] = g_symbols[j];
            g_symbols[j] = tmp;
        }
    }
}

// 根据符号地址寻找符号名，同时返回地址相对于符号的偏移量
const char *dbg_sym_resolve(size_t addr, size_t *remain) {
    for (int i = g_symbol_num - 1; i >= 0; --i) {
        if (g_symbols[i].addr > addr) {
            continue;
        }
        if (g_symbols[i].addr + g_symbols[i].size > addr) {
            *remain = addr - g_symbols[i].addr;
            return g_symbols[i].name;
        }
    }

    *remain = addr;
    return NULL;
}

// 根据符号名定位地址，未找到则返回 NULL
void *dbg_sym_locate(const char *name) {
    for (int i = 0; i < g_symbol_num; ++i) {
        if (0 == strcmp(g_symbols[i].name, name)) {
            return (void *)g_symbols[i].addr;
        }
    }

    return NULL;
}

// 打印函数调用栈
void dbg_show_trace(void **frames, int depth) {
    for (int i = 0; i < depth; ++i) {
        uint64_t remain;
        const char *name = dbg_sym_resolve((size_t)frames[i], &remain);
        if (NULL == name) {
            name = "(null)";
        }
        dbg_print("--> 0x%016lx = %s + 0x%lx\n", (size_t)frames[i], name, remain);
    }
}






//------------------------------------------------------------------------------
// 处理断言失效
//------------------------------------------------------------------------------

#define TRACE_DEPTH 32

void assert_fail(const char *file, const char *func, int line) {
    dbg_print("Assert failed %s:%s:%d\n", file, func, line);

    void *frames[TRACE_DEPTH];
    int depth = unwind(frames, TRACE_DEPTH);
    dbg_show_trace(frames + 1, depth - 1);
}
