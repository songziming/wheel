#include <wheel.h>

void sys_init(uint32_t eax, uint32_t ebx) {
    char *video = (char*)0xb8000;
    const char msg[] = "wheel kernel running";
    for (int i = 0; msg[i]; ++i) {
        video[2*i] = msg[i];
        video[2*i+1] = 0x1f;
    }
    while (1) {}
}
