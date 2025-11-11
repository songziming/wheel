#ifndef KSTRING_H
#define KSTRING_H

#include <wheel.h>

void  *kmemset(void *buf, int x, size_t n);
void  *kmemcpy(void *dst, const void *src, size_t n);
int    kmemcmp(const void *s1, const void *s2, size_t n);
size_t kstrlen(const char *s);
int    kstrcmp(const char *s1, const char *s2);

#endif // KSTRING_H
