#ifndef DEBUG_H
#define DEBUG_H

#include <wheel.h>

typedef void (*log_func_t)(const char *, size_t);
extern log_func_t g_log_func;

void logk(const char *fmt, ...) PRINTF(1,2);
void log_stacktrace();
void panic(const char *fmt, ...) PRINTF(1,2);

#if defined(UNIT_TEST)
    #include <assert.h>
    #define ASSERT assert
#elif defined(DEBUG)
    void assertion_fail(const char *file, int line, const char *func);
    #define ASSERT(x) do { \
        if (!(x)) { \
            assertion_fail(__FILE__, __LINE__, __func__); \
        } \
    } while (0)
#else
    #define ASSERT(...)
#endif

#endif // DEBUG_H
