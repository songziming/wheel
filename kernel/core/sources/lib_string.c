// 一套与标准库兼容的字符串操作函数
// 都是弱符号，arch 模块可以提供更高效的实现

#include <lib_string.h>

// #include <debug.h>
#define ASSERT(...)


WEAK size_t kstrlen(const char *s) {
    const char *p = s;
    for (; *p; ++p) {}
    return (size_t)(p - s);
}

WEAK size_t kstrnlen(const char *s, size_t n) {
    const char *p = s;
    for (; *p && n; ++p, --n) {}
    return (size_t)(p - s);
}

WEAK int kstrcmp(const char *s1, const char *s2) {
    for (; *s1 || *s2; ++s1, ++s2) {
        if (*s1 != *s2) {
            return (int)*s1 - (int)*s2;
        }
    }
    return 0;
}

WEAK int kstrncmp(const char *s1, const char *s2, size_t n) {
    for (; (*s1 || *s2) && n; ++s1, ++s2, --n) {
        if (*s1 != *s2) {
            return (int)*s1 - (int)*s2;
        }
    }
    return 0;
}

WEAK char *kstrcpy(char *dst, const char *src) {
    ASSERT((dst <= src) || (dst > src + strlen(src)));
    for (char *d = dst; (*d = *src); ++d, ++src) {}
    return dst;
}

// n 是输出字符串 dst 的最大长度，包含结尾的 '\0'
// 如果 src 过长，dst 结尾会缺少终止字符 '\0'
// 如果 src 过短，dst 剩余部分会填充 '\0'
WEAK char *kstrncpy(char *dst, const char *src, size_t n) {
    ASSERT(n > 0);
    ASSERT((dst <= src) || (dst > src + strnlen(src, n)));
    char *bak = dst;
    for (; *src && n; ++src, ++dst, --n) {
        *dst = *src;
    }
    for (; n; ++dst, --n) {
        *dst = '\0';
    }
    return bak;
}

WEAK void *kmemset(void *buf, int x, size_t n) {
    uint8_t v = x;
    uint8_t *d = (uint8_t *)buf;
    for (; n > 0; ++d, --n) {
        *d = v;
    }
    return buf;
}

WEAK int kmemcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (; n > 0; ++p1, ++p2, --n) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
    }
    return 0;
}

// memcpy 不允许内存重叠
WEAK void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    ASSERT((d <= s) || (d >= s + n));
    for (; n; ++s, ++d, --n) {
        *d = *s;
    }
    return dst;
}

WEAK void *kmemmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if ((s < d) && (d < s + n)) {
        d += n - 1;
        s += n - 1;
        for (; n; --s, --d, --n) {
            *d = *s;
        }
    } else if (s != d) {
        for (; n; ++s, ++d, --n) {
            *d = *s;
        }
    }
    return dst;
}
