#ifndef DEBUG_H
#define DEBUG_H

#include <common.h>

void set_log_func(void (*func)(const char *, size_t));
void log(const char *fmt, ...);
void log_stacktrace();

void assertion_fail(const char *file, int line, const char *func);
#define ASSERT(x) do { \
    if (!(x)) { \
        assertion_fail(__FILE__, __LINE__, __func__); \
    } \
} while (0)

#endif // DEBUG_H
