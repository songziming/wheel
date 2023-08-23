#ifndef ARCH_CONFIG_H
#define ARCH_CONFIG_H

// 需要和 layout.ld 保持一致
#define KERNEL_LOAD_ADDR        0x0000000000100000UL    //  1M
#define KERNEL_TEXT_BASE        0xffffffff80000000UL    // -2G
#define AP_REALMODE_ADDR        0x8000                  // 32K

// 所有物理内存映射到 canonical hole 之后
#define DIRECT_MAP_BASE         0xffff800000000000UL

// 这段虚拟地址用于动态分配
#define DYNAMIC_MAP_ADDR        0xffff900000000000UL
#define DYNAMIC_MAP_END         0xffffb00000000000UL

#define PAGE_SIZE               0x1000
#define PAGE_SHIFT              12
#define VIRT_ADDR_BITS          48
#define VA_SIGN_EXTEND          16

#define INIT_STACK_SIZE         0x1000 // pre-task stack

#define EARLY_CONST_BUFF_SIZE   0x4000
#define EARLY_ALLOC_BUFF_SIZE   0x400000 // 需要容纳完整 framebuffer

#define EARLY_CONST_HEAP_SIZE   0x4000
#define EARLY_HEAP_SIZE         0x400000 // 需要容纳完整 framebuffer

#define INT_STACK_RANK          1   // 8K
#define ROOT_STACK_SIZE         0x1000
#define IDLE_STACK_SIZE         256

#define TASK_STACK_RANK         2

#define PAGE_CACHE_MAX          32  // 每个 CPU 最多保留多少页的缓存
#define SYS_CLOCK_FREQ          60  // 系统时钟中断频率

#endif // ARCH_CONFIG_H
