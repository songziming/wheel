#include <libc.h>

int main(void) {
    const char *msg = "hello from user program\n";

    // 使用 syscall.h 提供的 write() 包装函数
    write(1, msg, 26);
    yield();
    exit(0);
    return 0;
}
