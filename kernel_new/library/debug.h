#ifndef DEBUG_H
#define DEBUG_H

#include <common.h>

void set_log_func(void (*func)(const char *, size_t));

void log(const char *fmt, ...);

#endif // DEBUG_H
