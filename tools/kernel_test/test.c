#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

#include "test.h"

#define RED   "\033[0;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"


extern char __start_testitems;
extern char __stop_testitems;

static int suite_maxlen = 0;
static int test_maxlen = 0;

static testitem_t *curr_item = NULL;
static jmp_buf curr_env;

static int total_success_cnt = 0;
static int total_fail_cnt = 0;


void report_assert_fail(const char *file, const char *func, int line, const char *msg, ...) {
    printf(RED "fail\n" RESET); // TODO 红色
    printf(" -> %s : %s : %d\n", file, func, line);

    va_list args;
    va_start(args, msg);
    vfprintf(stdout, msg, args);
    va_end(args);

    longjmp(curr_env, 1);
}


static void run_test_case(testitem_t *item) {
    printf("%*s::%-*s ... ", suite_maxlen, item->_suite, test_maxlen, item->_test);

    if (item->_setup) {
        item->_setup();
    }

    if (0 == setjmp(curr_env)) {
        item->_func();
        printf(GREEN "success\n" RESET); // TODO 绿色
        ++total_success_cnt;
    } else {
        // 发生了 EXPECT 失败，通过 longjmp 跳转到这里
        ++total_fail_cnt;
    }

    // 不管成功还是失败，都要释放资源
    if (item->_teardown) {
        item->_teardown();
    }
}


int main() {
    // TODO 解析命令行，可以只运行某些 suite

    size_t num = (size_t)(&__stop_testitems - &__start_testitems) / sizeof(testitem_t *);
    testitem_t **items = (testitem_t **)&__start_testitems;

    // TODO 对 items 排序，相同的 suite 放在一起
    //      或者假设编译器按照 item 在文件中出现的顺序排列

    // 统计 suite、test 名称字符串长度上限
    for (size_t i = 0; i < num; ++i) {
        int l1 = (int)strlen(items[i]->_suite);
        int l2 = (int)strlen(items[i]->_test);
        if (suite_maxlen < l1) {
            suite_maxlen = l1;
        }
        if (test_maxlen < l2) {
            test_maxlen = l2;
        }
    }

    // 逐一运行每个测试
    for (size_t i = 0; i < num; ++i) {
        curr_item = items[i];
        run_test_case(curr_item);
    }

    // 统计信息
    printf("\n");
    printf("total success: %d\n", total_success_cnt);
    printf("total fail:    %d\n", total_fail_cnt);

    return 0;
}
