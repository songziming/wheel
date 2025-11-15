#include <gtest/gtest.h>
#include <string>

extern "C" {
    #include "format.h"
    #include <stdio.h>
}

static void dst_print(void *user, const char *s, size_t n) {
    std::string *str = (std::string*)user;
    str->append(s, n);
}

static size_t cmp_print(const char *wanted, const char *fmt, ...) {
    std::string dst;
    static char tmp[8];

    va_list args;
    va_start(args, fmt);
    size_t len = format(tmp, sizeof(tmp), dst_print, &dst, fmt, args);
    va_end(args);

    EXPECT_STREQ(wanted, dst.c_str());
    return len;
}

// 验证分步格式化
TEST(Fmt, Split) {
    cmp_print("abcdefghijklmn", "abcdefghijklmn");
    cmp_print("1234_hello_5678", "1234%s5678", "_hello_");
    cmp_print("1234_hello_world_5678", "1234%s5678", "_hello_world_");
    cmp_print("hello_1234567890_world", "hello_%ld_world", 1234567890L);
}

// 验证格式化字符串长度计算
TEST(Fmt, Length) {
    EXPECT_EQ(6, snprintk(NULL, 0, "%d", 123456));
    EXPECT_EQ(8, snprintk(NULL, 0, "%8d", 123456));
    EXPECT_EQ(8, snprintk(NULL, 0, "%*d", 8, 123456));
    EXPECT_EQ(4, snprintk(NULL, 0, "%.4s", "hello"));
    EXPECT_EQ(4, snprintk(NULL, 0, "%.*s", 4, "hello"));
    EXPECT_EQ(0, snprintk(NULL, 0, "%.0s", "goodbye"));
}

// 超过目标 buffer 长度的情况
TEST(Fmt, Crop) {
    char buff[8];
    const char s1[] = "hello";
    const char s2[] = "lorem ipsum";

    snprintk(buff, sizeof(buff), s1);
    EXPECT_EQ(strlen(buff), strlen(s1));

    snprintk(buff, sizeof(buff), s2);
    EXPECT_EQ(strlen(buff), sizeof(buff) - 1);
}

// 检查错误处理，不认识的 specifier、空的模式串
TEST(Fmt, Error) {
    EXPECT_EQ(0, snprintk(NULL, 0, NULL, 123));

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-invalid-specifier"
    char buff[16];
    snprintk(buff, sizeof(buff), "in%valid", 123);
    EXPECT_STREQ(buff, "invalid");
#pragma clang diagnostic pop
}

// 验证字符串格式化
TEST(Fmt, String) {
    char buff[1024];

    snprintk(buff, sizeof(buff), "%s", "");
    EXPECT_STREQ(buff, "");

    snprintk(buff, sizeof(buff), "hello %s", (char*)NULL);
    EXPECT_STREQ(buff, "hello (null)");

    snprintk(buff, sizeof(buff), "hello %s.", "world");
    EXPECT_STREQ(buff, "hello world.");

    snprintk(buff, sizeof(buff), "hello %7s.", "world");
    EXPECT_STREQ(buff, "hello   world.");

    snprintk(buff, sizeof(buff), "hello %-7s.", "world");
    EXPECT_STREQ(buff, "hello world  .");
}

// 特殊情况
TEST(Fmt, Special) {
    char buff[1024];

    snprintk(buff, sizeof(buff), "a%cc", 'b');
    EXPECT_STREQ(buff, "abc");

    snprintk(buff, sizeof(buff), "a%3cc", 'b');
    EXPECT_STREQ(buff, "a  bc");

    snprintk(buff, sizeof(buff), "a%-3cc", 'b');
    EXPECT_STREQ(buff, "ab  c");

    snprintk(buff, sizeof(buff), "100%%");
    EXPECT_STREQ(buff, "100%");

    snprintk(buff, sizeof(buff), "%p", (void*)0xdeadbeef);
    EXPECT_STREQ(buff, "0xdeadbeef");
}

// 验证不同数据类型
TEST(Fmt, Types) {
    cmp_print("ef",         "%hhx", 0xdeadbeef);
    cmp_print("beef",       "%hx",  0xdeadbeef);
    cmp_print("10deadbeef", "%jx",  0x10deadbeefL);
    cmp_print("10deadbeef", "%zx",  0x10deadbeefL);
    cmp_print("10deadbeef", "%tx",  0x10deadbeefL);
    cmp_print("10deadbeef", "%lx",  0x10deadbeefL);
    cmp_print("10deadbeef", "%llx", 0x10deadbeefLL);
    cmp_print("-1",         "%hhd", 0x111111ff);
    cmp_print("-1",         "%hd",  0x1111ffff);
    cmp_print("-1",         "%jd",  (long)0xffffffffffffffffL);
    cmp_print("-1",         "%zd",  (long)0xffffffffffffffffL);
    cmp_print("-1",         "%td",  (long)0xffffffffffffffffL);
    cmp_print("-1",         "%ld",  (long)0xffffffffffffffffL);
    cmp_print("-1",         "%lld", (long long)0xffffffffffffffffLL);
}

// 数值类型的格式化
TEST(Fmt, Number) {
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
        EXPECT_STREQ(buff, test_cases[i].res);
    }
}
