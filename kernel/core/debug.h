#ifndef DEBUG_H
#define DEBUG_H

#include <stddef.h>

typedef void (*log_func_t)(const char *, size_t);
extern log_func_t g_log_func;

void logk(const char *fmt, ...);
void panic(const char *fmt, ...);
void assertion_fail(const char *file, int line, const char *func);

#define ASSERT(x) do { \
    if (!(x)) { \
        assertion_fail(__FILE__, __LINE__, __func__); \
    } \
} while (0)

#endif // DEBUG_H
