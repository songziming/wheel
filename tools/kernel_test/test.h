#ifndef TEST_H
#define TEST_H

// 这是为内核代码定制的单元测试框架，可以用于 C 和 C++
// 内核的某些函数与标准库函数重名，会出现链接冲突
// 这时可以使用 C++ 将其包含在命名空间中，避免符号冲突

typedef struct testitem {
    const char *_suite;
    const char *_test;

    void (*_setup)();
    void (*_teardown)();
    void (*_func)();
} testitem_t;

#ifdef __cplusplus
extern "C"
#endif
void report_test_fail(const char *file, const char *func, int line, const char *msg, ...);

#define _CONCAT(a,b) a##b
#define CONCAT(a,b) _CONCAT(a,b)

#define FUNCNAME(suite, test) CONCAT(suite, test)
#define ITEMNAME(suite, test) CONCAT(item_, FUNCNAME(suite, test))
#define ITEMPTR(suite, test)  CONCAT(ptr_, FUNCNAME(suite, test))

#define TESTSECT __attribute__((section("testitems"), used))

#define TEST_F(suite, test, setup, teardown)    \
    void FUNCNAME(suite, test)();               \
    static testitem_t ITEMNAME(suite, test) = { \
        #suite,                                 \
        #test,                                  \
        setup,                                  \
        teardown,                               \
        FUNCNAME(suite, test)                   \
    };                                          \
    static TESTSECT testitem_t *ITEMPTR(suite, test) \
        = & ITEMNAME(suite, test); \
    void FUNCNAME(suite, test)()

#define TEST(suite, test) TEST_F(suite, test, NULL, NULL)

// EXPECT 一旦失败，就停止当前的测试项
// EXPECT 宏不一定直接用在 TEST 函数中，可能用于 TEST 调用的子函数
// 因此不能直接用 return 返回，但是可以在 report 里面用 longjmp 返回

#define EXPECT_TRUE(cond, ...) do { \
    if (!(cond)) { \
        report_test_fail(__FILE__, __func__, __LINE__, ""__VA_ARGS__); \
    } \
} while (0)

#endif // TEST_H
