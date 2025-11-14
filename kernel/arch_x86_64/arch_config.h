#ifndef ARCH_X86_64_ARCH_CONFIG_H
#define ARCH_X86_64_ARCH_CONFIG_H

// 与 x86_64 平台相关的配置参数

#define PAGE_SHIFT  12
#define PAGE_SIZE   0x1000

// 内核地址空间
#define KERNEL_REAL_ADDR    0x8000               // 32K，实模式启动代码位置
#define KERNEL_LOAD_ADDR    0x0000000000100000UL //  1M
#define KERNEL_TEXT_ADDR    0xffffffff80000000UL // -2G
#define DIRECT_MAP_ADDR     0xffff800000000000UL // 物理内存映射地址（共 16TB）

#define INIT_STACK_SIZE 0x1000

#endif // ARCH_X86_64_ARCH_CONFIG_H
