#include <libc.h>

int main() {
    float a = 0.1;
    float b = 0.2;
    float c = a + b;
    uint32_t u = *(uint32_t*)&c;

    char repr[9];
    for (int i = 0; i < 8; ++i) {
        int digit = (u & 15);
        u >>= 4;
        if (digit < 10) {
            repr[i] = digit + '0';
        } else {
            repr[i] = digit - 10 + 'A';
        }
    }
    repr[8] = '\0';
    sys_print("floating repr is:");
    sys_print(repr);
    return 0;
}
