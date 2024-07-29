#ifndef DEBUG_H
#define DEBUG_H

#include <common.h>

void set_log_func(void (*func)(const char *, size_t));
void log(const char *fmt, ...);

void assertion_fail(const char *file, const char *func, int line);
#define ASSERT(x) do { \
    if (!(x)) { \
        assertion_fail(__FILE__, __func__, __LINE__); \
    } \
} while (0)

#endif // DEBUG_H
