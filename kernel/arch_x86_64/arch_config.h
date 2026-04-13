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

// 预留空间大小
#define EARLY_RO_SIZE   0x1000
#define EARLY_RW_SIZE   0x400000    // 需要容纳 framebuf
#define INIT_STACK_SIZE 0x1000      // 启动使用的临时栈
#define INT_STACK_SIZE  0x1000      // 中断栈

// 中断向量号
#define VEC_GSI_BASE        0x40
#define VEC_IPI_RESCHED     0xd0    // 触发任务切换
#define VEC_IPI_STOPALL     0xd1    // 所有 CPU 都停止
#define VEC_LOAPIC_TIMER    0xe0
#define VEC_LOAPIC_ERROR    0xf0
#define VEC_LOAPIC_THERMAL  0xf1
#define VEC_LOAPIC_SPURIOUS 0xff    // spurious 向量号最后 4-bit 必须都是 1

#endif // ARCH_X86_64_ARCH_CONFIG_H
