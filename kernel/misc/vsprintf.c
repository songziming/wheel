#include <wheel.h>

// printf format specifier rule:
// %[flags][width][.precision][qualifier]specifier

#define FLG_LEFT   0x01     // '-', left-justified
#define FLG_PLUS   0x02     // '+', print sign even for positive number
#define FLG_SPACE  0x04     // ' ', put an extra space if no sign
#define FLG_PREFIX 0x08     // '#', print 0, 0x or 0X before number
#define FLG_ZERO   0x10     // '0', pad with zero
#define FLG_LOWER  0x20     //      use lowercase when formatting hexidecimal
#define FLG_SIGN   0x40     //      number type is signed

// `width` is the minimum number of chars to be printed, padded with blank spaces
// `precision` is the maximum number of chars to be printed
static char * fmt_string(char * buf, char * end, char * str, u32 flags, int width, int precision) {
    int slen = strlen(str);
    if (0 < precision && precision < slen) {
        slen = precision;
    }

    if (!(flags & FLG_LEFT)) {
        for (; width > slen; --width, ++buf) {
            if (buf < end) {
                *buf = ' ';
            }
        }
    }

    int plen = MIN(slen, end - buf);
    if (plen > 0) {
        memcpy(buf, str, plen);
    }
    buf += slen;

    for (; width > slen; --width, ++buf) {
        if (buf < end) {
            *buf = ' ';
        }
    }

    return buf;
}

// print number field, given base, width, precision and other attributes
static char * fmt_number(char * buf, char * end, u64 num, int base, u32 flags, int width, int precision) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[65];

    char locase = (flags & FLG_LOWER) ? 0x20 : 0;
    char sign = 0;
    if (flags & FLG_SIGN) {
        if ((s64) num < 0) {
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

    if (flags & FLG_PREFIX) {
        if (16 == base) {
            width -= 2;         // prefix with `0x` or `0X`
        } else if (0 != num) {
            --width;            // prefix with `0`
        }
    }

    int i = 0;  // number of digits printed
    while (1) {
        tmp[i++] = digits[num % base] | locase;
        num /= base;
        --width;
        --precision;
        if (num == 0) {
            break;
        }
    }

    // number of padding
    if (precision > 0) {
        width -= precision;
    }

    // pad with space (outer level)
    if (!(flags & (FLG_ZERO | FLG_LEFT))) {
        while (--width > 0) {
            if (buf < end) {
                *buf = ' ';
            }
            ++buf;
        }
    }

    // print sign
    if (0 != sign) {
        if (buf < end) {
            *buf = sign;
        }
        ++buf;
    }

    // print `0x` / `0` prefix
    if ((flags & FLG_PREFIX) && (10 != base)) {
        if ((16 == base) || (0 != num)) {
            if (buf < end) {
                *buf = '0';
            }
            ++buf;
        }
        if (16 == base) {
            if (buf < end) {
                *buf = 'X' | locase;
            }
            ++buf;
        }
    }

    // zero or space padding
    if (!(flags & FLG_LEFT)) {
        char pad = (flags & FLG_ZERO) ? '0' : ' ';
        while (--width >= 0) {
            if (buf < end) {
                *buf = pad;
            }
            ++buf;
        }
    }

    // zero padding
    while (i <= --precision) {
        if (buf < end) {
            *buf = '0';
        }
        ++buf;
    }

    // actual digits of result
    while (i > 0) {
        if (buf < end) {
            *buf = tmp[--i];
        }
        ++buf;
    }

    // trailing space padding
    while (--width >= 0) {
        if (buf < end) {
            *buf = ' ';
        }
        ++buf;
    }

    return buf;
}

// return the number of chars required, not counting the terminating null
usize vsnprintf(char * buf, usize size, const char * fmt, va_list args) {
    char * ptr = buf;
    char * end = ptr + size;
    if (end < ptr) {
        end = (char *) -1;
        size = end - ptr;
    }

    while (*fmt) {
        if ('%' != *fmt) {
            if (ptr < end) {
                *ptr = *fmt;
            }
            ++ptr;
            ++fmt;
            continue;
        }

        // parameters used in formatting
        u32 flags     =  0;
        int width     = -1;
        int precision = -1;
        int qualifier =  0;
        int base      = 10;

        // parse flags
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

        // parse width
        if (isdigit(*fmt)) {
            for (width = 0; isdigit(*fmt); ++fmt) {
                width = width * 10 + (*fmt - '0');
            }
        } else if ('*' == *fmt) {
            ++fmt;
            width = va_arg(args, int);
        }

        // parse precision
        if ('.' == *fmt) {
            ++fmt;
            if (isdigit(*fmt)) {
                for (precision = 0; isdigit(*fmt); ++fmt) {
                    precision = precision * 10 + (*fmt - '0');
                }
            } else if ('*' == *fmt) {
                ++fmt;
                precision = va_arg(args, int);
            }
        }

        // parse qualifier
        switch (*fmt) {
        case 'h':   // also <hh>
        case 'l':   // also <ll>
        case 'z':
        case 't':
            ++fmt;
            qualifier = *fmt;
            if (qualifier == *fmt) {
                ++fmt;
                qualifier = toupper(qualifier);
            }
            break;
        }

        // parse specifier
        switch (*fmt) {
        case '%':
            if (ptr < end) {
                *ptr = '%';
            }
            ++ptr;
            ++fmt;
            continue;
        case 'c':
            if (FLG_LEFT != (flags & FLG_LEFT)) {
                while (--width > 0) {
                    if (ptr < end) {
                        *ptr = ' ';
                    }
                    ++ptr;
                }
            }
            if (ptr < end) {
                *ptr = (unsigned char) va_arg(args, int);
            }
            ++ptr;
            ++fmt;
            while (--width > 0) {
                if (ptr < end) {
                    *ptr = ' ';
                }
                ++ptr;
            }
            continue;
        case 's':
            ptr = fmt_string(ptr, end, va_arg(args, char *), flags, width, precision);
            ++fmt;
            continue;
        default:
            // specifier not recognized
            continue;
        case 'o':
            base = 8;
            ++fmt;
            break;
        case 'p':
        case 'x':
            flags |= FLG_LOWER;
            // fallthrough
        case 'X':
            base = 16;
            ++fmt;
            break;
        case 'd':
        case 'i':
            flags |= FLG_SIGN;
            // fallthrough
        case 'u':
            base = 10;
            ++fmt;
            break;
        }

        // retrieve number
        u64 num = 0;
        switch (qualifier) {
        case 'H':
            if (flags & FLG_SIGN) {
                num = (char) va_arg(args, int);
            } else {
                num = (unsigned char) va_arg(args, int);
            }
            break;
        case 'h':
            if (flags & FLG_SIGN) {
                num = (short) va_arg(args, int);
            } else {
                num = (unsigned short) va_arg(args, int);
            }
            break;
        case 'l':
            if (flags & FLG_SIGN) {
                num = va_arg(args, long);
            } else {
                num = va_arg(args, unsigned long);
            }
            break;
        case 'L':
            if (flags & FLG_SIGN) {
                num = va_arg(args, s64);
            } else {
                num = va_arg(args, u64);
            }
            break;
        case 't':
            num = va_arg(args, ptrdiff_t);
            break;
        case 'z':
            num = va_arg(args, size_t);
            break;
        default:
            num = va_arg(args, unsigned int);
            break;
        }

        ptr = fmt_number(ptr, end, num, base, flags, width, precision);
    }

    if (size > 0) {
        if (ptr < end) {
            end = ptr + 1;
        }
        end[-1] = '\0';
    }

    return (usize) ptr - (usize) buf;
}

usize snprintf(char * buf, usize size, const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    usize num = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return num;
}
