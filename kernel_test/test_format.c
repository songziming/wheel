#include "test.h"
#include <stdlib.h>
#include <string.h>
#include <library/format.h>



static size_t dst_len = 0;
static size_t dst_max = 0;
static char  *dst_buf = NULL;

static void dst_prepare() {
    dst_max = 16;
    dst_buf = malloc(dst_max);
}

static void dst_teardown() {
    dst_len = 0;
    dst_max = 0;
    free(dst_buf);
}

static void dst_print(const char *s, size_t n) {
    while (dst_len + n >= dst_max) {
        dst_max <<= 1;
        dst_buf = realloc(dst_buf, dst_max);
    }
    memcpy(dst_buf + dst_len, s, n);
    dst_len += n;
    dst_buf[dst_len] = '\0';
}

static void print_n_check(const char *wanted, const char *fmt, ...) {
    static char tmp[8];
    va_list args;
    va_start(args, fmt);
    dst_prepare();
    format(tmp, sizeof(tmp), dst_print, fmt, args);
    va_end(args);

    EXPECT(0 == strcmp(wanted, dst_buf), "expected %s, got %s", wanted, dst_buf);
    dst_teardown();
}

TEST(Format, Split) {
    print_n_check("abcdefghijklmn", "abcdefghijklmn");
    print_n_check("1234_hello_5678", "1234%s5678", "_hello_");
    print_n_check("1234_hello_world_5678", "1234%s5678", "_hello_world_");
    print_n_check("hello_1234567890_world", "hello_%ld_world", 1234567890L);
}

TEST(Format, Length) {
    EXPECT(6 == snprintk(NULL, 0, "%d", 123456));
    EXPECT(4 == snprintk(NULL, 0, "%.4s", "hello"));
    EXPECT(0 == snprintk(NULL, 0, "%.0s", "goodbye"));
}

TEST(Format, String) {
    char buff[1024];

    snprintk(buff, sizeof(buff), "hello %s", "world");
    EXPECT(0 == strcmp(buff, "hello world"));

    snprintk(buff, sizeof(buff), "%s", "");
    EXPECT(0 == strcmp(buff, ""));
}

TEST(Format, Number) {
    static const struct {
        const char *fmt;
        int         val;
        const char *res;
    } test_cases[] = {
        //  测试 width、precision、对齐方式的处理
        { "%04d",       12,     "0012"  },
        { "%.3d",       12,     "012"   },
        { "%3d",        12,     " 12"   },
        { "%-3d",       12,     "12 "   },
        { "%+3d",       12,     "+12"   },
        { "%+-5d",      12,     "+12  " },
        { "%+- 5d",     12,     "+12  " },
        { "%- 5d",      12,     " 12  " },
        { "% d",        12,     " 12"   },
        { "%0-5d",      12,     "12   " },
        { "%-05d",      12,     "12   " },

        // precision 取零时，数字零不显示，但八进制前缀、width、符号仍有效
        { "%.0d",       0,      ""      },
        { "%.0o",       0,      ""      },
        { "%#.0d",      0,      ""      },
        { "%#.0o",      0,      "0"     },
        { "%#.0x",      0,      ""      },
        { "%2.0u",      0,      "  "    },
        { "%02.0u",     0,      "  "    },
        { "%2.0d",      0,      "  "    },
        { "%02.0d",     0,      "  "    },
        { "% .0d",      0,      " "     },
        { "%+.0d",      0,      "+"     },

        // 十六进制常规显示
        { "%x",         63,     "3f"    },
        { "%X",         63,     "3F"    },
        { "%#x",        63,     "0x3f"  },
        { "%#05x",      63,     "0x03f" },

        // 八进制常规显示
        { "%o",         15,     "17"    },
        { "%#o",        15,     "017"   },

        // 八进制特殊情况（corner case）
        { "%#o",        0,      "0"     },
        { "%#.0o",      0,      "0"     },
        { "%#.1o",      0,      "0"     },
        { "%#o",        1,      "01"    },
        { "%#.0o",      1,      "01"    },
        { "%#.1o",      1,      "01"    },
        { "%#04o",      1,      "0001"  },
        { "%#04.0o",    1,      "  01"  },
        { "%#04.1o",    1,      "  01"  },
        { "%04o",       1,      "0001"  },
        { "%04.0o",     1,      "   1"  },
        { "%04.1o",     1,      "   1"  },
    };
    size_t case_count = sizeof(test_cases) / sizeof(test_cases[0]);

    char buff[1024];
    for (int i = 0; i < case_count; ++i) {
        snprintk(buff, sizeof(buff), test_cases[i].fmt, test_cases[i].val);
        EXPECT(0 == strcmp(buff, test_cases[i].res));
    }
}
