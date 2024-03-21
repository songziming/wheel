#ifndef DEBUG_H
#define DEBUG_H

#include <def.h>

typedef void (*log_func_t)(const char *s, size_t n);
void set_log_func(log_func_t func);
PRINTF(1,2) void klog(const char *fmt, ...);

#if defined(UNIT_TEST)
    #include <assert.h>
    #define ASSERT assert
#elif defined(DEBUG)
    void handle_assert_fail(const char *file, const char *func, int line);
    #define ASSERT(x) do { \
        if (!(x)) { \
            handle_assert_fail(__FILE__, __func__, __LINE__); \
        } \
    } while (0)
#else
    #define ASSERT(...)
#endif

INIT_TEXT void symtab_init(void *ptr, uint32_t entsize, uint32_t num);
size_t sym_locate(const char *name);
const char *sym_resolve(size_t addr, size_t *rela);

void print_frames(const size_t *frames, int num);
void klog_stacktrace();

#endif // DEBUG_H