#ifndef STRING_H
#define STRING_H

#include <common.h>

size_t def_strlen(const char *s);
// int    def_strcmp(const char *s1, const char *s2);
void  *def_memset(void *buf, int x, size_t n);
int    def_memcmp(const void *s1, const void *s2, size_t n);
void  *def_memcpy(void *dst, const void *src, size_t n);

size_t strlen(const char *s);
// int    strcmp(const char *s1, const char *s2);
void  *memset(void *buf, int x, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);

#endif // STRING_H
