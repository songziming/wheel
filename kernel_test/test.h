#ifndef TEST_H
#define TEST_H

// 单元测试支持，可以与内核编译在一起，也可以作为应用程序在 host 环境下运行

typedef struct testitem {
    const char *_suite;
    const char *_test;

    void (*_setup)();
    void (*_teardown)();
    void (*_func)();
} testitem_t;

#define _CONCAT(a,b) a##b
#define CONCAT(a,b) _CONCAT(a,b)

#define FUNCNAME(suite, test) CONCAT(suite, test)
#define ITEMNAME(suite, test) CONCAT(item_, FUNCNAME(suite, test))
#define ITEMPTR(suite, test)  CONCAT(ptr_, FUNCNAME(suite, test))

#define TESTSECT __attribute__((section("testitems"),used,retain))

#define TEST_F(suite, test, setup, teardown)    \
void FUNCNAME(suite, test)();                   \
TESTSECT testitem_t ITEMNAME(suite, test) = {   \
    #suite,                                     \
    #test,                                      \
    setup,                                      \
    teardown,                                   \
    FUNCNAME(suite, test)                       \
};                                              \
void FUNCNAME(suite, test)()

#define TEST(suite, test) TEST_F(suite, test, NULL, NULL)

void report_test_fail(const char *file, int line, const char *msg, ...);

#define EXPECT(cond, ...) do { \
    if (!(cond)) { \
        report_test_fail(__FILE__, __LINE__, ""__VA_ARGS__); \
    } \
} while (0)

#define EXPECT_EQ(v1, v2, ...) do { \
    __typeof__(v1) __v1 = (v1); \
    __typeof__(v2) __v2 = (v2); \
    if (__v1 != __v2) {         \
        report_test_fail(__FILE__, __LINE__, ""__VA_ARGS__);    \
    }   \
} while (0)

#endif // TEST_H
