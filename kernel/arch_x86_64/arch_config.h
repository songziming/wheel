#ifndef ARCH_X86_64_ARCH_CONFIG_H
#define ARCH_X86_64_ARCH_CONFIG_H

//------------------------------------------------------------------------------
// 与 x86_64 平台相关的配置参数
//------------------------------------------------------------------------------

#define PAGE_SHIFT  12
#define PAGE_SIZE   0x1000UL

//------------------------------------------------------------------------------
// 内核地址空间
//------------------------------------------------------------------------------

#define KERNEL_REAL_ADDR    0x8000               // 32K，实模式启动代码位置
#define KERNEL_LOAD_ADDR    0x0000000000100000UL //  1M
#define KERNEL_TEXT_ADDR    0xffffffff80000000UL // -2G

// 直接映射的内存区域（16TB）
// 我们最多支持 16TB-4GB 物理内存，因为页编号是 uint32，再减去不存在的零页号
// 通过这个地址读写 MMIO
#define IDENTITY_MAP_ADDR   0xffff800000000000UL
#define IDENTITY_MAP_END    0xffff900000000000UL

// 防越界的直接映射内存区域映射，保留两倍最大物理内存（即 32TB）
// pool/heap 申请的物理内存映射在这里，offset=paddr*2
// 即使申请到相邻的物理页，映射在这里也是不连续的，guard-page 防止越界
#define GUARDED_IDMAP_ADDR  0xffff900000000000UL
#define GUARDED_IDMAP_END   0xffffb00000000000UL

// 内核动态分配内存的映射区域（16TB）
// 这里的内存是 kernel vmspace 管理的
#define DYNAMIC_ZONE_START  0xffffb00000000000UL
#define DYNAMIC_ZONE_END    0xffffc00000000000UL

//------------------------------------------------------------------------------
// 预留空间大小
//------------------------------------------------------------------------------

#define EARLY_RO_SIZE       0x1000
#define EARLY_RW_SIZE       0x800000    // 需要容纳 framebuf
#define INIT_STACK_SIZE     0x1000      // 启动使用的临时栈
#define INT_STACK_SIZE      0x1000      // 中断栈
#define KERNEL_HEAP_SIZE    0x8000      // 内核堆

//------------------------------------------------------------------------------
// 中断向量号
//------------------------------------------------------------------------------

#define VEC_GSI_BASE        0x40
#define VEC_IPI_STOPALL     0xd0    // 所有 CPU 都停止
#define VEC_IPI_RESCHED     0xd1    // 触发任务切换
#define VEC_IPI_INVLPG      0xd2    // TLB-shootdown
#define VEC_LOAPIC_TIMER    0xe0
#define VEC_LOAPIC_ERROR    0xf0
#define VEC_LOAPIC_THERMAL  0xf1
#define VEC_LOAPIC_SPURIOUS 0xff    // spurious 向量号最后 4-bit 必须都是 1

#endif // ARCH_X86_64_ARCH_CONFIG_H
