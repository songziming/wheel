#ifndef ARCH_X86_64_CONFIG_H
#define ARCH_X86_64_CONFIG_H

#define KERNEL_LOAD_ADDR    0x0000000000100000UL    //  1M
#define KERNEL_TEXT_ADDR    0xffffffff80000000UL    // -2G
#define KERNEL_REAL_ADDR    0x8000  // 32K，实模式启动代码位置

#define INIT_STACK_SIZE     0x1000  // 初始化阶段使用的栈的大小

#endif // ARCH_X86_64_CONFIG_H
