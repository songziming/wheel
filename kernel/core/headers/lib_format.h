#ifndef LIB_FORMAT_H
#define LIB_FORMAT_H

#include <base.h>

typedef void (*format_cb_t)(void *para, const char *s, size_t n);

size_t format(char *buf, size_t n, format_cb_t cb, void *para, const char *fmt, va_list args);
size_t vsnprintk(char *buf, size_t n, const char *fmt, va_list args);
PRINTF(3, 4) size_t snprintk(char *buf, size_t n, const char *fmt, ...);

#endif // LIB_FORMAT_H
