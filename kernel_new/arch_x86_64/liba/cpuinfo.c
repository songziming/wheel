#include <common.h>
#include "rw.h"


// Linux kernel 代码中，cpuid 是一个设备文件，因此 cpuid.c 只能看到向系统注册设备，将文件读写转换为 cpuid 指令的逻辑
// 真正执行 cpuid 的代码位于 cpuflags.c，相关代码还有 cpucheck.c


static CONST uint32_t g_cpu_vendor[3];
// static CONST uint32_t g_cpu_brand[4];


// 使用内联汇编时，必须加上 volatile 关键字，这样编译器才知道内联命令有副作用
// 否则可能优化出错


static void cpuid(uint32_t id, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(id));
}

// 有些 cpuid 调用还要通过 ecx 传入数量
static void cpuid_count(uint32_t id, uint32_t num, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(id), "c"(num));
}


// 参考 linux/arch/x86/boot/cpuflags.c, 函数 get_cpuflags(void)
INIT_TEXT void parse_cpuinfo() {
    // uint32_t a, b, c, d;
    uint32_t dummy;

    // 这种方法得到的 cpuid-max-level 只适用于 intel cpu
    // AMD cpu 需要用其他方法获得 CPUID 最大功能号
    uint32_t max_level_intel;
    cpuid(0, &max_level_intel, &g_cpu_vendor[0], &g_cpu_vendor[2], &g_cpu_vendor[1]);

    uint32_t max_level_amd;
    cpuid(0x80000000, &max_level_amd, &dummy, &dummy, &dummy);


    // volatile __asm__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));

    // memcpy(g_cpu_vendor, (uint32_t[]){ b,d,c }, 12);
}
