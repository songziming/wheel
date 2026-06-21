#include <libc.h>

int main() {
    float a = 0.1;
    float b = 0.2;
    if (0.3 == a + b) {
        sys_print("no rounding error\n");
    } else {
        sys_print("rounding error exist\n");
    }
    return 0;
}
