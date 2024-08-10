#ifndef STRING_H
#define STRING_H

#include <common.h>

WEAK size_t strlen(const char *s);
WEAK int    strcmp(const char *s1, const char *s2);
WEAK int    strncmp(const char *s1, const char *s2, size_t n);
WEAK char  *strncpy(char *dst, const char *src, size_t n);

WEAK void  *memset(void *buf, int x, size_t n);
WEAK int    memcmp(const void *s1, const void *s2, size_t n);
WEAK void  *memcpy(void *dst, const void *src, size_t n);

#ifdef UNIT_TEST
size_t kstrlen(const char *s, size_t n);
size_t kstrcmp(const char *s1, const char *s2, size_t n);
size_t kstrncmp(const char *s1, const char *s2, size_t n);
char  *kstrncpy(char *dst, const char *src, size_t n);
void  *kmemset(void *buf, int x, size_t n);
int    kmemcmp(const void *s1, const void *s2, size_t n);
void  *kmemcpy(void *dst, const void *src, size_t n);
// void  *kmemmove(void *dst, const void *src, size_t n);
#endif // UNIT_TEST

#endif // STRING_H
