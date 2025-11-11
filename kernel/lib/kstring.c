#include "kstring.h"

void *kmemset(void *buf, int x, size_t n) {
    uint8_t v = x;
    uint8_t *d = (uint8_t*)buf;
    for (; n > 0; ++d, --n) {
        *d = v;
    }
    return buf;
}

// 类似 memcpy，不考虑内存重叠的情况
void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    if (d == s) {
        return dst;
    }
    for (; n; ++s, ++d, --n) {
        *d = *s;
    }
    return dst;
}

int kmemcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t*)s1;
    const uint8_t *p2 = (const uint8_t*)s2;
    for (; n > 0; ++p1, ++p2, --n) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
    }
    return 0;
}

size_t kstrlen(const char *s) {
    const char *p = s;
    for (; *p; ++p) {}
    return (size_t)(p - s);
}

int kstrcmp(const char *s1, const char *s2) {
    for (; *s1 || *s2; ++s1, ++s2) {
        if (*s1 != *s2) {
            return (int)*s1 - (int)*s2;
        }
    }
    return 0;
}
