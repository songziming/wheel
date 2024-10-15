#include "test.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>


#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define RESET   "\033[0m"
#define PASS    "\u2713"
#define FAIL    "\u2717"


extern char __start_testitems;
extern char __stop_testitems;

static size_t suite_len = 0;
static size_t test_len = 0;

static testitem_t *curr_item = NULL;
static jmp_buf curr_env;

static int total_pass_cnt = 0;
static int total_fail_cnt = 0;


void report_test_fail(const char *file, int line, const char *msg, ...) {
    fprintf(stdout, RED FAIL RESET "\n");
    fprintf(stdout, " -> %s:%d: ", file, line);

    va_list args;
    va_start(args, msg);
    vfprintf(stdout, msg, args);
    va_end(args);

    fprintf(stdout, "\n");
    longjmp(curr_env, 1);
}


static void run_test_case(testitem_t *item) {
    fprintf(stdout, "%*s::%-*s ... ",
        (int)suite_len, item->_suite,
        (int)test_len, item->_test);

    if (item->_prepare) {
        item->_prepare();
    }

    if (0 == setjmp(curr_env)) {
        item->_func();
        fprintf(stdout, GREEN PASS RESET "\n");
        ++total_pass_cnt;
    } else {
        // 发生了 EXPECT 失败，通过 longjmp 跳转到这里
        ++total_fail_cnt;
    }

    // 不管成功还是失败，都要释放资源
    if (item->_teardown) {
        item->_teardown();
    }
}


// TODO 将测试中产生的输出保存下来？case 跑完再一起打印？
static void intercepted_log_func(const char *s, size_t n) {
    fprintf(stdout, "%.*s", (int)n, s);
}


// TODO 解析命令行，可以只运行某些 suite
// TODO 解析环境变量 LLVM_PROFILE_FILE，设置输出文件位置
#include <arch_intf.h>
#include <devices/acpi.h>
#include <library/debug.h>
int test_called_by_host();

int main() {
    // 拦截内核的调试输出
    set_log_func(intercepted_log_func);

    size_t num = (size_t)(&__stop_testitems - &__start_testitems) / sizeof(testitem_t);
    testitem_t *items = (testitem_t*)&__start_testitems;

    // 统计 suite、test 名称字符串长度上限
    for (size_t i = 0; i < num; ++i) {
        size_t l1 = strlen(items[i]._suite);
        size_t l2 = strlen(items[i]._test);
        if (suite_len < l1) {
            suite_len = l1;
        }
        if (test_len < l2) {
            test_len = l2;
        }
    }

    // 逐一运行每个测试
    for (size_t i = 0; i < num; ++i) {
        curr_item = &items[i];
        run_test_case(curr_item);
    }

    // 统计信息
    fprintf(stdout, "\n");
    fprintf(stdout, "total success: %d\n", total_pass_cnt);
    fprintf(stdout, "total fail:    %d\n", total_fail_cnt);

    acpi_table_find("ABCD", -123);
    fprintf(stdout, "interrupt depth = %d\n", cpu_int_depth());
    fprintf(stdout, "interrupt depth = %d\n", test_called_by_host());

    return total_fail_cnt;
}
