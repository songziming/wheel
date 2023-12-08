#ifndef DEBUG_H
#define DEBUG_H

#include <base.h>
#include <elf.h>


typedef void (*dbg_print_func_t)(const char *s, size_t n);
extern dbg_print_func_t g_print_func;


INIT_TEXT void dbg_sym_init(Elf64_Shdr *secs, int n);
const char *dbg_sym_resolve(size_t addr, size_t *remain);
void *dbg_sym_locate(const char *name);
void dbg_show_trace(void **frames, int depth);


PRINTF(1, 2) void dbg_print(const char *fmt, ...);
void assert_fail(const char *file, const char *func, int line);


#if defined(UNIT_TEST)
    #include <assert.h>
    #define ASSERT assert
#elif defined(DEBUG)
    #define ASSERT(x) do { \
        if (!(x)) { \
            assert_fail(__FILE__, __func__, __LINE__); \
        } \
    } while (0)
#else
    #define ASSERT(...)
#endif

#endif // DEBUG_H
