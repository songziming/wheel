#include <test.h>
#include <stdlib.h>
#include <string.h>

#include "../sources/lib_format.c"


static void printer(char **dst, const char *s, size_t n) {
    int len = *dst ? strlen(*dst) : 0;
    *dst = (char *)realloc(*dst, len + n + 1);
    memcpy(*dst + len, s, n);
    (*dst)[len + n] = '\0';
}

static char *my_vprint(const char *fmt, ...) {
    static char tmp[8];
    char *str = NULL;
    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), (format_cb_t)printer, &str, fmt, args);
    va_end(args);
    return str;
}

static void compare_and_free(const char *s1, char *s2) {
    EXPECT_TRUE(0 == strcmp(s1, s2));
    free(s2);
}

TEST(Format, Split) {
    compare_and_free("abcdefghijklmn", my_vprint("abcdefghijklmn"));
    compare_and_free("1234_hello_5678", my_vprint("1234%s5678", "_hello_"));
}

TEST(Format, Length) {
    EXPECT_TRUE(6 == snprintk(NULL, 0, "%d", 123456));
    EXPECT_TRUE(4 == snprintk(NULL, 0, "%.4s", "hello"));
    EXPECT_TRUE(0 == snprintk(NULL, 0, "%.0s", "goodbye"));
}

TEST(Format, String) {
    char buff[1024];

    snprintk(buff, sizeof(buff), "hello %s", "world");
    EXPECT_TRUE(0 == strcmp(buff, "hello world"));

    snprintk(buff, sizeof(buff), "%s", "");
    EXPECT_TRUE(0 == strcmp(buff, ""));
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
        EXPECT_TRUE(0 == strcmp(buff, test_cases[i].res));
    }
}
