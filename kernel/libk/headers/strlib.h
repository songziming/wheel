#ifndef STRLIB_H
#define STRLIB_H

#include <base.h>

WEAK size_t kstrlen (const char *s);
WEAK size_t kstrnlen(const char *s, size_t n);
WEAK int    kstrcmp (const char *s1, const char *s2);
WEAK int    kstrncmp(const char *s1, const char *s2, size_t n);
WEAK char  *kstrcpy (char *dst, const char *src);
WEAK char  *kstrncpy(char *dst, const char *src, size_t n);

WEAK void  *kmemset (void *buf, int x, size_t n);
WEAK int    kmemcmp (const void *s1, const void *s2, size_t n);
WEAK void  *kmemcpy (void *dst, const void *src, size_t n);
WEAK void  *kmemmove(void *dst, const void *src, size_t n);

#endif // STRLIB_H
