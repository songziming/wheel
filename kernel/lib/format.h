#ifndef FORMAT_H
#define FORMAT_H

#include <wheel.h>

// format 可以使用有限长度的 buff，多次调用回调返回格式化结果
// 不会写入终止符 '\0'
// 返回字符串完整格式化之后的长度，不含结尾的 '\0'
typedef void (*format_cb_t)(void *user, const char **s, size_t *len);
size_t format(char *buf, size_t n, format_cb_t func, void *user, const char *fmt, va_list args);

// 返回完整字符串的长度（不含结尾的 '\0'）
// 最多写入 n 的字节（包含结尾的 '\0'）
size_t vsnprintk(char *buf, size_t n, const char *fmt, va_list args);
PRINTF(3,4) size_t snprintk(char *buf, size_t n, const char *fmt, ...);

#endif // FORMAT_H
