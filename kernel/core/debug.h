#ifndef DEBUG_H
#define DEBUG_H

#include <stddef.h>

typedef void (*log_func_t)(const char *, size_t);
extern log_func_t g_log_func;

void logk(const char *fmt, ...);
void panic(const char *fmt, ...);

#endif // DEBUG_H
