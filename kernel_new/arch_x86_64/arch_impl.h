#ifndef ARCH_IMPL_H
#define ARCH_IMPL_H

// 本文件相当于 board support package，但 wheel 只需要支持一个平台
// 包含系统编译配置，可被硬件无关部分引用

//------------------------------------------------------------------------------
// 硬件平台
//------------------------------------------------------------------------------

#define PAGE_SHIFT      12
#define PAGE_SIZE       0x1000

//------------------------------------------------------------------------------
// 内核地址空间布局
//------------------------------------------------------------------------------

#define KERNEL_LOAD_ADDR    0x0000000000100000UL //  1M
#define KERNEL_TEXT_ADDR    0xffffffff80000000UL // -2G
#define KERNEL_REAL_ADDR    0x8000  // 32K，实模式启动代码位置

#define DIRECT_MAP_ADDR     0xffff800000000000UL // 物理内存映射地址（共 16TB）
#define DYNAMIC_MAP_ADDR    0xffff900000000000UL // 动态映射范围（共 16TB）
#define DYNAMIC_MAP_END     0xffffa00000000000UL

//------------------------------------------------------------------------------
// 预留缓冲区大小
//------------------------------------------------------------------------------

#define INIT_STACK_SIZE 0x1000

#define EARLY_RO_SIZE   0x1000
#define EARLY_RW_SIZE   0x800000




// #ifdef C_FILE

// typedef struct regs {
//     uint64_t rax;
// } regs_t;

// #endif // C_FILE

#endif // ARCH_IMPL_H
