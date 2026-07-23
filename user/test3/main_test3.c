#include <libc.h>

int main(void) {
    sys_print("hello from user program");

    // yield();
    sys_exit(0);
    return 0;
}
