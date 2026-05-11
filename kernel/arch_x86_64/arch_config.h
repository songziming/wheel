#ifndef ARCH_X86_64_ARCH_CONFIG_H
#define ARCH_X86_64_ARCH_CONFIG_H

// 与 x86_64 平台相关的配置参数

#define PAGE_SHIFT  12
#define PAGE_SIZE   0x1000

// 内核地址空间
#define KERNEL_REAL_ADDR    0x8000               // 32K，实模式启动代码位置
#define KERNEL_LOAD_ADDR    0x0000000000100000UL //  1M
#define KERNEL_TEXT_ADDR    0xffffffff80000000UL // -2G
#define DIRECT_MAP_ADDR     0xffff800000000000UL // 物理内存映射地址（共 16TB - 4GB）
#define MMIO_WC_BASE        0xffff8FFF00000000UL // WC 映射区域（4GB），位于 direct map 之后

// 内核栈的映射区域（16TB）
#define STACK_ZONE_START    0xffff900000000000UL
#define STACK_ZONE_END      0xffffa00000000000UL

// 内存池映射的区域（16TB）
#define POOL_ZONE_START     0xffffa00000000000UL
#define POOL_ZONE_END       0xffffb00000000000UL


// 预留空间大小
#define EARLY_RO_SIZE   0x1000
#define EARLY_RW_SIZE   0x800000    // 需要容纳 framebuf
#define INIT_STACK_SIZE 0x1000      // 启动使用的临时栈
#define INT_STACK_SIZE  0x4000      // 中断栈

// 中断向量号
#define VEC_GSI_BASE        0x40
#define VEC_IPI_STOPALL     0xd0    // 所有 CPU 都停止
#define VEC_IPI_RESCHED     0xd1    // 触发任务切换
#define VEC_IPI_INVLPG      0xd2    // TLB-shootdown
// #define VEC_IPI_MIGRATE     0xd2    // 迁移任务到空闲 CPU
#define VEC_LOAPIC_TIMER    0xe0
#define VEC_LOAPIC_ERROR    0xf0
#define VEC_LOAPIC_THERMAL  0xf1
#define VEC_LOAPIC_SPURIOUS 0xff    // spurious 向量号最后 4-bit 必须都是 1

#endif // ARCH_X86_64_ARCH_CONFIG_H
