#include <lib_format.h>
#include <lib_string.h>


// 使用循环缓冲区接收格式化之后的字符串
// 缓冲区填满了，就执行回调函数输出，然后重置缓冲区
typedef struct fmt_context {
    char       *ptr;
    char       *buf;
    char       *end;
    format_cb_t cb;
    void       *para;
    size_t      len; // 已经通过 cb 输出的长度
} fmt_context_t;


static inline void fmt_flush(fmt_context_t *ctx) {
    if (NULL != ctx->cb) {
        size_t len = (size_t)(ctx->ptr - ctx->buf);
        ctx->cb(ctx->para, ctx->buf, len);
        ctx->ptr  = ctx->buf;
        ctx->len += len;
    }
}

static inline void fmt_print(fmt_context_t *ctx, char ch) {
    if (ctx->ptr >= ctx->end) {
        fmt_flush(ctx);
    }
    if (ctx->ptr < ctx->end) {
        *ctx->ptr = ch;
    }
    ++ctx->ptr;
}

static inline PURE int _isdigit(int c) {
    return (('0' <= c) && (c <= '9'));
}

static inline PURE int _toupper(int c) {
    return c & ~0x20;
}


// printf 格式说明符
// %[flags][width][.precision][qualifier]specifier
#define FLG_LEFT     0x01 // '-' 靠左对齐
#define FLG_PLUS     0x02 // '+' 正数也打印符号
#define FLG_SPACE    0x04 // ' ' 若没有符号，则多打印一个空格
#define FLG_PREFIX   0x08 // '#' 打印前缀（0、0x、0X）
#define FLG_ZERO     0x10 // '0' 补零
#define FLG_LOWER    0x20 //     使用小写字母（打印十六进制）
#define FLG_SIGNED   0x40 //     数值为有符号类型
#define FLG_NEGATIVE 0x80 //     数值为负


// 格式化字符串
static void fmt_string(fmt_context_t *ctx, const char *str, uint32_t flags, int width, int precision) {
    int slen = kstrlen(str);
    if ((0 <= precision) && (precision < slen)) {
        slen = precision;
    }

    if (0 == (flags & FLG_LEFT)) {
        for (; width > slen; --width) {
            fmt_print(ctx, ' ');
        }
    }

    for (int i = 0; i < slen; ++i, ++str) {
        fmt_print(ctx, *str);
    }

    for (; width > slen; --width) {
        fmt_print(ctx, ' ');
    }
}

// 格式化整型数字
static void fmt_number(fmt_context_t *ctx, uint64_t abs, int base, uint32_t flags, int width, int precision) {
    static const char DIGITS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char tmp[65];

    // 如果 precision 已经补零，width 就不能再补零
    if ((precision >= 0) && (width > precision)) {
        flags &= ~FLG_ZERO;
    }

    // 如果左对齐，则右侧只能补空格，不能补零
    if (flags & FLG_LEFT) {
        flags &= ~FLG_ZERO;
    }

    // 判断大小写
    char locase = (flags & FLG_LOWER) ? 0x20 : 0;

    // 数字转换成字符串，逆序储存在 tmp
    // 如果 precision 和数字都是零，那么不会打印任何数字
    int bits = 0;
    if (precision || abs) {
        do {
            tmp[bits++] = DIGITS[abs % base] | locase;
            abs /= base;
        } while (abs);
        precision -= bits;
        width -= bits;
    } else {
        // 不打印数字，就不能补零
        flags &= ~FLG_ZERO;
    }

    // 判断是否打印符号
    char sign = 0;
    if (flags & FLG_SIGNED) {
        if (flags & FLG_NEGATIVE) {
            sign = '-';
            --width;
        } else if (flags & FLG_PLUS) {
            sign = '+';
            --width;
        } else if (flags & FLG_SPACE) {
            sign = ' ';
            --width;
        }
    }

    // 判断是否打印前缀
    // 十六进制前缀 0x/0X 不属于数字，不占 precision
    // 但八进制前缀 0 属于数字，占一位 precision
    char prefix[3] = { 0, 0, 0 };
    if (flags & FLG_PREFIX) {
        if (bits && (16 == base)) {
            // 前缀不能独立存在
            width -= 2;
            prefix[0] = '0';
            prefix[1] = 'X' | locase;
        } else if (((1 != bits) || ('0' != tmp[0])) && (8 == base)) {
            // 如果数字本身就是 0，则会与前缀重复，此时不应打印先追
            // 如果 precision 为零，说明数字零并未打印，则可以打印前缀
            --precision;
            --width;
            prefix[0] = '0';
        }
    }

    // 长度尚不足 precision，则高位补零，填补的零也占据了 width
    int pad_zero = 0;
    if (precision > 0) {
        pad_zero = precision;
        width -= precision;
    }

    // 长度尚不足 width，需要根据 flags 填补空格或零
    // 如果 flags 规定补零，但因为 precision 已经填补了零，那么 width 改为补空格
    // 只能在数字左侧补零，如果左对齐，则数字右侧只能补空格
    int pad_space = 0;
    if (width > 0) {
        if (flags & FLG_ZERO) {
            pad_zero = width;
        } else {
            pad_space = width;
        }
    }

    // 如果右对齐，就在开头补空格
    if (0 == (flags & FLG_LEFT)) {
        for (int i = 0; i < pad_space; ++i) {
            fmt_print(ctx, ' ');
        }
    }

    // 打印符号
    if (0 != sign) {
        fmt_print(ctx, sign);
    }

    // 打印前缀
    for (int i = 0; prefix[i]; ++i) {
        fmt_print(ctx, prefix[i]);
    }

    // 补零
    for (int i = 0; i < pad_zero; ++i) {
        fmt_print(ctx, '0');
    }

    // 打印数值
    for (int i = 0; i < bits; ++i) {
        fmt_print(ctx, tmp[bits - i - 1]);
    }

    // 如果左对齐，就在末尾补空格
    if (flags & FLG_LEFT) {
        for (int i = 0; i < pad_space; ++i) {
            fmt_print(ctx, ' ');
        }
    }
}



// 格式化字符串，返回完整输出字符串的长度（不含结尾的零）
// 如果能完整地格式化，则返回0
// 如果不能完整格式化（目标buf不够大），则返回值表示fmt前面多少字节已经完成格式化
// 如果回调函数para为空，则最多打印n个字符到buf
size_t format(char *buf, size_t n, format_cb_t cb, void *para, const char *fmt, va_list args) {
    fmt_context_t ctx;
    ctx.ptr  = buf;
    ctx.buf  = buf;
    ctx.end  = buf + n;
    ctx.cb   = cb;
    ctx.para = para;
    ctx.len  = 0;

    while (*fmt) {
        if ('%' != *fmt) {
            fmt_print(&ctx, *fmt++);
            continue;
        }

        // 默认参数
        uint32_t flags     =  0;
        int      width     = -1;
        int      precision = -1;
        int      qualifier =  0;
        int      base      = 10;

        // 解析 flags，可能有多个
        int loop = 1;
        while (loop) {
            ++fmt;
            switch (*fmt) {
            case '-':   flags |= FLG_LEFT;    break;
            case '+':   flags |= FLG_PLUS;    break;
            case ' ':   flags |= FLG_SPACE;   break;
            case '#':   flags |= FLG_PREFIX;  break;
            case '0':   flags |= FLG_ZERO;    break;
            default:    loop   = 0;           break;
            }
        }

        // 解析 width
        if ('*' == *fmt) {
            ++fmt;
            width = va_arg(args, int);
        } else {
            width = 0;
            for (; _isdigit(*fmt); ++fmt) {
                width = width * 10 + (*fmt - '0');
            }
        }

        // 解析 precision
        if ('.' == *fmt) {
            ++fmt;
            if ('*' == *fmt) {
                ++fmt;
                precision = va_arg(args, int);
            } else {
                precision = 0;
                for (; _isdigit(*fmt); ++fmt) {
                    precision = precision * 10 + (*fmt - '0');
                }
            }
        }

        // 解析类型说明符 qualifier
        switch (*fmt) {
        case 'h':   // 以及 hh
        case 'l':   // 以及 ll
            qualifier = *fmt++;
            if (qualifier == *fmt) {
                ++fmt;
                qualifier = _toupper(qualifier);
            }
            break;
        case 'j':
        case 'z':
        case 't':
            qualifier = *fmt++;
            break;
        }

        // 解析说明符
        switch (*fmt) {
        case '%':
            ++fmt;
            fmt_print(&ctx, '%');
            continue;
        case 'c':
            if (FLG_LEFT != (flags & FLG_LEFT)) {
                while (--width > 0) {
                    fmt_print(&ctx, ' ');
                }
            }
            fmt_print(&ctx, (unsigned char)va_arg(args, int));
            ++fmt;
            while (--width > 0) {
                fmt_print(&ctx, ' ');
            }
            continue;
        case 's':
            fmt_string(&ctx, va_arg(args, char *), flags, width, precision);
            ++fmt;
            continue;
        default:
            // 遇到了不认识的说明符，直接忽略
            continue;
        case 'o':
            base = 8;
            ++fmt;
            break;
        case 'p':
            qualifier = 'L';
            flags |= FLG_PREFIX;
            // fallthrough
        case 'x':
            flags |= FLG_LOWER;
            // fallthrough
        case 'X':
            base = 16;
            ++fmt;
            break;
        case 'd':
        case 'i':
            flags |= FLG_SIGNED;
            // fallthrough
        case 'u':
            base = 10;
            ++fmt;
            break;
        }

        // 获取数值，并转换为绝对值
        uint64_t abs = 0;
        if (flags & FLG_SIGNED) {
            int64_t sn = 0;
            switch (qualifier) {
            case 'H': sn = (signed char)va_arg(args, int); break;
            case 'h': sn = (short)va_arg(args, int);       break;
            case 'l': sn = va_arg(args, long);             break;
            case 'L': sn = va_arg(args, long long);        break;
            case 'j': sn = va_arg(args, intmax_t);         break;
            case 'z': sn = va_arg(args, size_t);           break;
            case 't': sn = va_arg(args, ptrdiff_t);        break;
            default:  sn = va_arg(args, int);              break;
            }
            if (sn < 0) {
                flags |= FLG_NEGATIVE;
                abs = (uint64_t)-sn;
            } else {
                abs = (uint64_t)sn;
            }
        } else {
            switch (qualifier) {
            case 'H': abs = (unsigned char)va_arg(args, int);  break;
            case 'h': abs = (unsigned short)va_arg(args, int); break;
            case 'l': abs = va_arg(args, unsigned long);       break;
            case 'L': abs = va_arg(args, unsigned long long);  break;
            case 'j': abs = va_arg(args, uintmax_t);           break;
            case 'z': abs = va_arg(args, size_t);              break;
            case 't': abs = va_arg(args, ptrdiff_t);           break;
            default:  abs = va_arg(args, unsigned int);        break;
            }
        }

        fmt_number(&ctx, abs, base, flags, width, precision);
    }

    fmt_flush(&ctx);
    return ctx.len + (size_t)(ctx.ptr - ctx.buf);
}

size_t vsnprintk(char *buf, size_t n, const char *fmt, va_list args) {
    size_t len = format(buf, n, NULL, NULL, fmt, args);

    if (NULL != buf) {
        if (len < n) {
            buf[len] = '\0';
        } else {
            buf[n] = '\0';
        }
    }

    return len;
}

size_t snprintk(char *buf, size_t n, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t len = vsnprintk(buf, n, fmt, args);
    va_end(args);
    return len;
}
