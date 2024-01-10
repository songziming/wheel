#ifndef STR_H
#define STR_H

#include <def.h>

WEAK size_t strnlen(const char *s, size_t n);
WEAK int    strncmp(const char *s1, const char *s2, size_t n);
WEAK char  *strncpy(char *dst, const char *src, size_t n);

WEAK void  *memset(void *buf, int x, size_t n);
WEAK int    memcmp(const void *s1, const void *s2, size_t n);
WEAK void  *memcpy(void *dst, const void *src, size_t n);

#endif // STR_H
