// 测试用户程序中发生异常

#include <libc.h>

static void test_pf() {
    uint8_t *wild = (uint8_t*)16;
    wild[0] = 0xbe;
}

int main() {
    test_pf();
    return 0;
}
