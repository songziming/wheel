#ifndef ARCH_IMPL_H
#define ARCH_IMPL_H

// 包含各种编译配置，被系统平台无关部分引用
// 可以重命名为 arch_config.h

#define PAGE_SHIFT      12
#define PAGE_SIZE       0x1000

#define INIT_STACK_SIZE 0x1000

#define EARLY_RO_SIZE   0x1000
#define EARLY_RW_SIZE   0x1000




// #ifdef C_FILE

// typedef struct regs {
//     uint64_t rax;
// } regs_t;

// #endif // C_FILE

#endif // ARCH_IMPL_H
