// 初始化程序

#include <base.h>

void sys_init(uint32_t ebx) {
    (void)ebx;

    char *video = (char *)0xb8000;
    char *s = "hello world!";

    for (int i = 0; s[i]; ++i) {
        video[2*i+0] = s[i];
        video[2*i+1] = 0x1f;
    }

    while (1) {}
}

void sys_init_ap(uint32_t ebx) {
    (void)ebx;
}
