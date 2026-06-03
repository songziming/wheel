// 优化版本的标准库函数，替代 lib/kstring.c 的默认实现

// 处理器支持 fast-string operation（P6 开始，Nehalem 改进）
// 能自动按 16 字节执行字符串操作，即使指令为 movsb、stosb
// fast-string operation 由 IA32_MISC_ENABLE[0] 控制，默认开启

// 如果使用 SSE、AVX 指令还能更快，但不适合在内核态使用

.global kmemcpy
.global kmemset

.text
.code64

// %rdi: dst
// %rsi: src
// %rdx: nbytes
kmemcpy:
    movq    %rdi, %rax // 返回值
    movq    %rdx, %rcx
    rep movsb
    ret

// %rdi: dst
// %rsi: val
// %rdx: nbytes
kmemset:
    movq    %rsi, %rax
    movq    %rdx, %rcx
    movq    %rdi, %rdx // 目标地址也是返回值
    rep stosb
    movq    %rdx, %rax // 返回值
    ret
