#ifndef STRLIB_H
#define STRLIB_H

#include <def.h>

WEAK size_t slen(const char *s, size_t n);
WEAK int    scmp(const char *s1, const char *s2, size_t n);
WEAK char  *scpy(char *dst, const char *src, size_t n);

WEAK void  *bset(void *buf, int x, size_t n);
WEAK int    bcmp(const void *s1, const void *s2, size_t n);
WEAK void  *bcpy(void *dst, const void *src, size_t n);

#endif // STRLIB_H
