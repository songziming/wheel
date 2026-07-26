#include <libc.h>

static void num2str(uint32_t n, char s[9]) {
    for (int i = 0; i < 8; ++i) {
        int digit = (n & 15);
        n >>= 4;
        if (digit < 10) {
            s[i] = digit + '0';
        } else {
            s[i] = digit - 10 + 'A';
        }
    }
    s[8] = '\0';
}

int main() {
    float a = 0.1;
    float b = 0.2;
    float c = a + b;

    char repr[9];
    num2str(*(uint32_t*)&c, repr);

    sys_print("floating repr is: <0x");
    sys_print(repr);
    sys_print(">\n");

    float d = c / 0.0;
    num2str(*(uint32_t*)&c, repr);
    sys_print("div by zero result <0x");
    sys_print(repr);
    sys_print(">\n");

    return 0;
}
