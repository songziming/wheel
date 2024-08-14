// 一套与标准库兼容的字符串操作函数
// 避免使用和标准库相同的函数名，避免单元测试符号冲突
// 都是弱符号，arch 模块可以提供更高效的版本，覆盖默认实现

#include "string.h"


size_t def_strlen(const char *s) {
    const char *p = s;
    for (; *p; ++p) {}
    return (size_t)(p - s);
}

int def_strcmp(const char *s1, const char *s2) {
    for (; *s1 || *s2; ++s1, ++s2) {
        if (*s1 != *s2) {
            return (int)*s1 - (int)*s2;
        }
    }
    return 0;
}

void *def_memset(void *buf, int x, size_t n) {
    uint8_t v = x;
    uint8_t *d = (uint8_t *)buf;
    for (; n > 0; ++d, --n) {
        *d = v;
    }
    return buf;
}

int def_memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (; n > 0; ++p1, ++p2, --n) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
    }
    return 0;
}

// 类似 memcpy，不考虑内存重叠的情况
void *def_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s) {
        return dst;
    }

    for (; n; ++s, ++d, --n) {
        *d = *s;
    }

    return dst;
}

#ifndef UNIT_TEST

size_t strlen(const char *s)                            WEAKALIAS("def_strlen");
int    strcmp(const char *s1, const char *s2)           WEAKALIAS("def_strcmp");
void  *memset(void *buf, int x, size_t n)               WEAKALIAS("def_memset");
int    memcmp(const void *s1, const void *s2, size_t n) WEAKALIAS("def_memcmp");
void  *memcpy(void *dst, const void *src, size_t n)     WEAKALIAS("def_memcpy");

#endif // UNIT_TEST
