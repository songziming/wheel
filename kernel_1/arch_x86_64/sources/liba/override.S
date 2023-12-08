// 这里定义的符号替换默认版本
// 提供硬件相关的高效实现

.global memcpy
.global memset

.text
.code64

// 新的处理器支持 enhanced rep movsb
// 如果传入的参数是对齐的，会自动优化

memcpy:
    // %rdi dst
    // %rsi src
    // %rdx num
    movq    %rdi, %rax
    movq    %rdx, %rcx
    rep movsb
    ret

memset:
    // %rdi dst
    // %rsi val
    // %rdx num
    movq    %rsi, %rax
    movq    %rdi, %rsi // 返回地址
    movq    %rdx, %rcx
    rep stosb
    movq    %rsi, %rax
    ret
