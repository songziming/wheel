#include <libc.h>

int main() {
    sys_print("hello from user program\n");
    _syscall(0, 1);
    return 0;
}
