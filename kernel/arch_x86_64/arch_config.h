#ifndef ARCH_X86_64_CONFIG_H
#define ARCH_X86_64_CONFIG_H

#define KERNEL_LOAD_ADDR    0x0000000000100000UL    //  1M
#define KERNEL_TEXT_ADDR    0xffffffff80000000UL    // -2G
#define KERNEL_REAL_ADDR    0x8000  // 32K，实模式启动代码位置

#define AP_BOOT_MAGIC       0xdeadbeef

#define DIRECT_MAP_ADDR     0xffff800000000000UL

#define INIT_STACK_SIZE     0x1000      // 初始化阶段使用的栈的大小
#define EARLY_RO_SIZE       0x2000      // 只读预留内存大小
#define EARLY_RW_SIZE       0x2000      // 读写预留内存大小

#define PAGE_SIZE           0x1000
#define PAGE_SHIFT          12

#define INT_STACK_SIZE      0x1000      // 中断栈

#endif // ARCH_X86_64_CONFIG_H
