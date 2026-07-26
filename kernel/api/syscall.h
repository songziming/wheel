#ifndef SYSCALL_H
#define SYSCALL_H

#include <stddef.h>
#include <stdint.h>

//==============================================================================
// 系统调用编号
//==============================================================================

enum {
    SYS_exit    = 0,  // void exit(int code)
    SYS_write   = 1,  // ssize_t write(int fd, const void *buf, size_t len)
    SYS_read    = 2,  // ssize_t read(int fd, void *buf, size_t len)
    SYS_open    = 3,  // int open(const char *path, int flags)
    SYS_close   = 4,  // int close(int fd)
    SYS_sleep   = 5,  // int sleep(unsigned ms)
    SYS_getpid  = 6,  // int getpid(void)
    SYS_yield   = 7,  // void yield(void)
    SYS_mmap    = 8,  // void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
    SYS_munmap  = 9,  // int munmap(void *addr, size_t len)
};

//==============================================================================
// 用户态系统调用包装函数（inline assembly）
//==============================================================================

// x86_64 syscall ABI:
//   rax = 调用编号
//   rdi, rsi, rdx, r10, r8, r9 = 参数 1–6
//   rcx = 返回 RIP（被 syscall 覆盖）
//   r11 = 返回 RFLAGS（被 syscall 覆盖）
//   返回值在 rax

// x86_64 调用约定：rdi, rsi, rdx, rcx, r8, r9
// 其中 rcx 会被 syscall 指令破坏，我们替换为 r10

// 通用 syscall 调用（0–6 个参数），由具体包装函数调用
static inline size_t __syscall6(size_t id,
        size_t a1, size_t a2, size_t a3,
        size_t a4, size_t a5, size_t a6) {
    size_t ret;
    register size_t r10 __asm__("r10") = a4;
    register size_t r8  __asm__("r8")  = a5;
    register size_t r9  __asm__("r9")  = a6;

    // syscall 指令会破坏 rcx/r11 两个寄存器
    // 因此系统调用不能使用标准调用约定
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(id), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline size_t __syscall3(size_t id, size_t a1, size_t a2, size_t a3) {
    return __syscall6(id, a1, a2, a3, 0, 0, 0);
}

static inline size_t __syscall2(size_t id, size_t a1, size_t a2) {
    return __syscall6(id, a1, a2, 0, 0, 0, 0);
}

static inline size_t __syscall1(size_t id, size_t a1) {
    return __syscall6(id, a1, 0, 0, 0, 0, 0);
}

static inline size_t __syscall0(size_t id) {
    return __syscall6(id, 0, 0, 0, 0, 0, 0);
}

#endif // SYSCALL_H
