#ifndef DEBUG_H
#define DEBUG_H

#include <base.h>

typedef void (*log_func_t)(const char *s, size_t n);

void set_log_func(log_func_t func);
PRINTF(1, 2) void klog(const char *fmt, ...);

#if defined(UNIT_TEST)
    #include <assert.h>
    #define ASSERT assert
#elif defined(DEBUG)
    #define ASSERT(x) do { \
        if (!(x)) { \
            klog("Assert failed %s:%s:%d\n", __FILE__, __func__, __LINE__); \
            vmshutdown(1); \
        } \
    } while (0)
#else
    #define ASSERT(...)
#endif

INIT_TEXT void symtab_init(void *ptr, uint32_t shndx, uint32_t num, uint32_t entsize);
#ifdef DEBUG
INIT_TEXT void symtab_show();
#endif

#endif // DEBUG_H
