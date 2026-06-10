#ifndef ARCH_X86_64_MEM_THISCPU_RW_H
#define ARCH_X86_64_MEM_THISCPU_RW_H

#include <wheel.h>

// thiscpu 直接读写，无需通过指针计算

// 使用 %gs 段寄存器读取内存
// 变量必须是公开符号，不能为 static，因为汇编和 C 代码是两个编译单元

// 如果是 static 变量，只能使用 RIP-相对寻址
// 相对寻址访问 static 变量的方法：
// ASMV("movq $1,%0" : "=m"(static_var_to_write));

// 无条件使用 RIP-relative addressing，这样的指令更短
// 注意，lea 计算的是 offset，也就是段寄存器并不会生效

// 更新 thiscpu 变量需要加上 memory clobber
// 告知编译器内存可能发生修改

#define _THISCPU_GET(op, var) ({ \
    __typeof__(var) dst; \
    ASMV(op " %%gs:%1,%0" : "=r"(dst) : "m"(var)); \
    dst; \
})
#define _THISCPU_SET(op, var, val) ({ \
    ASMV(op " %1,%%gs:%0" : "+m"(var) : "r"(val) : "memory"); \
})
#define _THISCPU_XADD(op, var, val) ({ \
    __typeof__(var) old = val; \
    ASMV(op " %0,%%gs:%1" : "+r"(old), "+m"(var) :: "memory"); \
    old; \
})

#define THISCPU_GET(var) _Generic((var), \
    int8_t:   _THISCPU_GET("movb", var), \
    uint8_t:  _THISCPU_GET("movb", var), \
    int16_t:  _THISCPU_GET("movw", var), \
    uint16_t: _THISCPU_GET("movw", var), \
    int32_t:  _THISCPU_GET("movl", var), \
    uint32_t: _THISCPU_GET("movl", var), \
    int64_t:  _THISCPU_GET("movq", var), \
    uint64_t: _THISCPU_GET("movq", var), \
    default:  _THISCPU_GET("movq", var)  \
)
#define THISCPU_SET(var, val) _Generic((var), \
    int8_t:   _THISCPU_SET("movb", var, val), \
    uint8_t:  _THISCPU_SET("movb", var, val), \
    int16_t:  _THISCPU_SET("movw", var, val), \
    uint16_t: _THISCPU_SET("movw", var, val), \
    int32_t:  _THISCPU_SET("movl", var, val), \
    uint32_t: _THISCPU_SET("movl", var, val), \
    int64_t:  _THISCPU_SET("movq", var, val), \
    uint64_t: _THISCPU_SET("movq", var, val), \
    default:  _THISCPU_SET("movq", var, val)  \
)
#define THISCPU_ADD(var, val) _Generic((var), \
    int8_t:   _THISCPU_SET("addb", var, val), \
    uint8_t:  _THISCPU_SET("addb", var, val), \
    int16_t:  _THISCPU_SET("addw", var, val), \
    uint16_t: _THISCPU_SET("addw", var, val), \
    int32_t:  _THISCPU_SET("addl", var, val), \
    uint32_t: _THISCPU_SET("addl", var, val), \
    int64_t:  _THISCPU_SET("addq", var, val), \
    uint64_t: _THISCPU_SET("addq", var, val), \
    default:  _THISCPU_SET("addq", var, val)  \
)

// thiscpu-var 不会与其他 cpu 竞争，不需要 lock 前缀
// 只会和中断竞争，单个指令总是原子的
#define THISCPU_XADD(var, val) _Generic((var), \
    int8_t:   _THISCPU_XADD("xaddb", var, val), \
    uint8_t:  _THISCPU_XADD("xaddb", var, val), \
    int16_t:  _THISCPU_XADD("xaddw", var, val), \
    uint16_t: _THISCPU_XADD("xaddw", var, val), \
    int32_t:  _THISCPU_XADD("xaddl", var, val), \
    uint32_t: _THISCPU_XADD("xaddl", var, val), \
    int64_t:  _THISCPU_XADD("xaddq", var, val), \
    uint64_t: _THISCPU_XADD("xaddq", var, val), \
    default:  _THISCPU_XADD("xaddq", var, val)  \
)

#endif // ARCH_X86_64_MEM_THISCPU_RW_H
