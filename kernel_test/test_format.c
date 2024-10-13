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

TEST(StringFormat, Split) {
    print_n_check("abcdefghijklmn", "abcdefghijklmn");
    print_n_check("1234_hello_5678", "1234%s5678", "_hello_");
    print_n_check("1234_hello_world_5678", "1234%s5678", "_hello_world_");
    print_n_check("hello_1234567890_world", "hello_%ld_world", 1234567890L);
}

TEST(StringFormat, Length) {
    EXPECT_EQ(6, snprintk(NULL, 0, "%d", 123456));
    EXPECT_EQ(8, snprintk(NULL, 0, "%8d", 123456));
    EXPECT_EQ(8, snprintk(NULL, 0, "%*d", 8, 123456));
    EXPECT_EQ(4, snprintk(NULL, 0, "%.4s", "hello"));
    EXPECT_EQ(4, snprintk(NULL, 0, "%.*s", 4, "hello"));
    EXPECT_EQ(0, snprintk(NULL, 0, "%.0s", "goodbye"));
}

TEST(StringFormat, Special) {
    char buff[1024];

    snprintk(buff, sizeof(buff), "a%cc", 'b');
    EXPECT_EQ(0, strcmp(buff, "abc"));

    snprintk(buff, sizeof(buff), "a%3cc", 'b');
    EXPECT_EQ(0, strcmp(buff, "a  bc"));

    snprintk(buff, sizeof(buff), "a%-3cc", 'b');
    EXPECT_EQ(0, strcmp(buff, "ab  c"));

    snprintk(buff, sizeof(buff), "100%%");
    EXPECT_EQ(0, strcmp(buff, "100%"));

    snprintk(buff, sizeof(buff), "%p", (void*)0xdeadbeef);
    EXPECT_EQ(0, strcmp(buff, "0xdeadbeef"));
}

TEST(StringFormat, String) {
    char buff[1024];

    snprintk(buff, sizeof(buff), "%s", "");
    EXPECT_EQ(0, strcmp(buff, ""));

    snprintk(buff, sizeof(buff), "hello %s", (char *)NULL);
    EXPECT_EQ(0, strcmp(buff, "hello (null)"));

    snprintk(buff, sizeof(buff), "hello %s.", "world");
    EXPECT_EQ(0, strcmp(buff, "hello world."));

    snprintk(buff, sizeof(buff), "hello %7s.", "world");
    EXPECT_EQ(0, strcmp(buff, "hello   world."));

    snprintk(buff, sizeof(buff), "hello %-7s.", "world");
    EXPECT_EQ(0, strcmp(buff, "hello world  ."));
}

TEST(StringFormat, Number) {
    static const struct {
        const char *fmt;
        long long   val;
        const char *res;
    } test_cases[] = {
        //  测试 width、precision、对齐方式的处理
        { "%04d",       12,     "0012"  },
        { "%.3d",       12,     "012"   },
        { "%3d",        12,     " 12"   },
        { "%d",        -12,     "-12"   },
        { "%4d",       -12,     " -12"  },
        { "%-3d",       12,     "12 "   },
        { "%+3d",       12,     "+12"   },
        { "%+-5d",      12,     "+12  " },
        { "%+- 5d",     12,     "+12  " },
        { "%- 5d",      12,     " 12  " },
        { "% d",        12,     " 12"   },
        { "%0-5d",      12,     "12   " },
        { "%-05d",      12,     "12   " },

        // 测试不同长度
        { "%hhx",   0xdeadbeef, "ef"    }, // char
        { "%hx",    0xdeadbeef, "beef"  }, // short
        { "%lx",    0x1234deadbeefL, "1234deadbeef"  }, // long
        { "%llx",   0x1234deadbeefLL, "1234deadbeef"  }, // long long

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
