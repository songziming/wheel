#ifndef ARCH_IMPL_H
#define ARCH_IMPL_H

// 本文件相当于 board support package，但 wheel 只需要支持一个平台
// 包含系统编译配置，可被硬件无关部分引用

//------------------------------------------------------------------------------
// 体系结构参数
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
// 预留缓冲区和栈大小
//------------------------------------------------------------------------------

#define INIT_STACK_SIZE 0x1000

#define EARLY_RO_SIZE   0x40000
#define EARLY_RW_SIZE   0x800000

#define INT_STACK_SIZE  0x1000

#define TASK_STACK_RANK 1   // 8K

//------------------------------------------------------------------------------
// 中断向量号
//------------------------------------------------------------------------------

#define VEC_GSI_BASE        0x40    // 外部中断
#define VEC_LOAPIC_TIMER    0xe0
#define VEC_LOAPIC_ERROR    0xfe
#define VEC_LOAPIC_SPURIOUS 0xff    // spurious 向量号最后 4-bit 必须是 f


//------------------------------------------------------------------------------
// 自定义结构体和宏
//------------------------------------------------------------------------------

#ifdef C_FILE

#include <stdint.h>

// Generic 展开之后必须是 expression，不能是 statement，而且表达式类型必须和类型 case 一致
// 内联汇编是 statement，不是 expression，不能直接放在 _Generic 里面
// 必须用 statement-expression，强行封装为 expression

#define GS_LOAD_EXPR(opsize, var) ({ \
    __typeof__(var) dst; \
    __asm__("mov" opsize " %%gs:(" #var "), %0" : "=r"(dst)); \
    dst; \
})

#define THISCPU_GET(var) _Generic((var),   \
     int8_t:  GS_LOAD_EXPR("b", var), \
    uint8_t:  GS_LOAD_EXPR("b", var), \
     int16_t: GS_LOAD_EXPR("w", var), \
    uint16_t: GS_LOAD_EXPR("w", var), \
     int32_t: GS_LOAD_EXPR("l", var), \
    uint32_t: GS_LOAD_EXPR("l", var), \
     int64_t: GS_LOAD_EXPR("q", var), \
    uint64_t: GS_LOAD_EXPR("q", var), \
    default:  GS_LOAD_EXPR("q", var)  \
)

#define THISCPU_SET(var, val) _Generic((var), \
     int8_t:  ({ __asm__("movb %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint8_t:  ({ __asm__("movb %0,%%gs:(" #var ")" :: "r"(val)); }), \
     int16_t: ({ __asm__("movw %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint16_t: ({ __asm__("movw %0,%%gs:(" #var ")" :: "r"(val)); }), \
     int32_t: ({ __asm__("movl %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint32_t: ({ __asm__("movl %0,%%gs:(" #var ")" :: "r"(val)); }), \
     int64_t: ({ __asm__("movq %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint64_t: ({ __asm__("movq %0,%%gs:(" #var ")" :: "r"(val)); }), \
    default:  ({ __asm__("movq %0,%%gs:(" #var ")" :: "r"(val)); })  \
)

#endif // C_FILE

#endif // ARCH_IMPL_H
