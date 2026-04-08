#ifndef ARCH_X86_64_MEM_THISCPU_RW_H
#define ARCH_X86_64_MEM_THISCPU_RW_H

#include <wheel.h>

// thiscpu 直接读写，无需通过指针计算

#define GS_LOAD_EXPR(opsize, var) ({ \
    __typeof__(var) dst; \
    ASMV("mov" opsize " %%gs:(" #var "), %0" : "=r"(dst)); \
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
     int8_t:  ({ ASMV("movb %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint8_t:  ({ ASMV("movb %0,%%gs:(" #var ")" :: "r"(val)); }), \
     int16_t: ({ ASMV("movw %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint16_t: ({ ASMV("movw %0,%%gs:(" #var ")" :: "r"(val)); }), \
     int32_t: ({ ASMV("movl %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint32_t: ({ ASMV("movl %0,%%gs:(" #var ")" :: "r"(val)); }), \
     int64_t: ({ ASMV("movq %0,%%gs:(" #var ")" :: "r"(val)); }), \
    uint64_t: ({ ASMV("movq %0,%%gs:(" #var ")" :: "r"(val)); }), \
    default:  ({ ASMV("movq %0,%%gs:(" #var ")" :: "r"(val)); })  \
)

#endif // ARCH_X86_64_MEM_THISCPU_RW_H
