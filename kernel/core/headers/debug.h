#ifndef DEBUG_H
#define DEBUG_H

#include <base.h>

typedef void (*dbg_print_func_t)(const char *s, size_t n);
extern dbg_print_func_t g_dbg_print_func;

PRINTF(1, 2) void dbg_print(const char *fmt, ...);
void report_assert_fail(const char *file, const char *func, int line);

#if defined(UNIT_TEST)
    #include <assert.h>
    #define ASSERT assert
#elif defined(DEBUG)
    #define ASSERT(x) do { \
        if (!(x)) { \
            report_assert_fail(__FILE__, __func__, __LINE__); \
        } \
    } while (0)
#else
    #define ASSERT(...)
#endif

INIT_TEXT void symtab_init(void *ptr, uint32_t shndx, uint32_t num, uint32_t entsize);

#endif // DEBUG_H
