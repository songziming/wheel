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

// 解析 ELF sections，解析符号表
// ELF 文件可能有多个符号表
INIT_TEXT void symtab_init(Elf64_Shdr *secs, int num) {
    ASSERT(NULL != secs);
    ASSERT(num > 0);

    // 统计符号表的数量，符号数量
    int symtab_num = 0;
    int symbol_num = 0;
    for (int i = 0; i < num; ++i) {
        Elf64_Shdr *sh = &secs[i];
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
