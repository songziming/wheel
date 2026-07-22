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
// 用户态：系统调用包装函数（inline assembly）
//==============================================================================

// x86_64 syscall ABI:
//   rax = 调用编号
//   rdi, rsi, rdx, r10, r8, r9 = 参数 1–6
//   rcx = 返回 RIP（被 syscall 覆盖）
//   r11 = 返回 RFLAGS（被 syscall 覆盖）
//   返回值在 rax

// 通用 syscall 调用（0–6 个参数），由具体包装函数调用
static inline int64_t __syscall6(int64_t num,
        int64_t a1, int64_t a2, int64_t a3,
        int64_t a4, int64_t a5, int64_t a6)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    register int64_t r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "syscall"
        : "+a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t __syscall3(int64_t num,
        int64_t a1, int64_t a2, int64_t a3)
{
    return __syscall6(num, a1, a2, a3, 0, 0, 0);
}

static inline int64_t __syscall2(int64_t num,
        int64_t a1, int64_t a2)
{
    return __syscall6(num, a1, a2, 0, 0, 0, 0);
}

static inline int64_t __syscall1(int64_t num, int64_t a1) {
    return __syscall6(num, a1, 0, 0, 0, 0, 0);
}

static inline int64_t __syscall0(int64_t num) {
    return __syscall6(num, 0, 0, 0, 0, 0, 0);
}

// ---- 用户态 API -------------------------------------------------------------

static inline void exit(int code) {
    __syscall1(SYS_exit, code);
    __builtin_unreachable();
}

static inline int64_t write(int fd, const void *buf, size_t len) {
    return __syscall3(SYS_write, fd, (int64_t)buf, (int64_t)len);
}

static inline int64_t read(int fd, void *buf, size_t len) {
    return __syscall3(SYS_read, fd, (int64_t)buf, (int64_t)len);
}

static inline int open(const char *path, int flags) {
    return (int)__syscall2(SYS_open, (int64_t)path, flags);
}

static inline int close(int fd) {
    return (int)__syscall1(SYS_close, fd);
}

static inline int sleep(unsigned ms) {
    return (int)__syscall1(SYS_sleep, (int64_t)ms);
}

static inline int getpid(void) {
    return (int)__syscall0(SYS_getpid);
}

static inline void yield(void) {
    __syscall0(SYS_yield);
}

static inline void *mmap(void *addr, size_t len, int prot, int flags,
        int fd, int64_t off)
{
    return (void*)__syscall6(SYS_mmap,
        (int64_t)addr, (int64_t)len, prot, flags, fd, off);
}

static inline int munmap(void *addr, size_t len) {
    return (int)__syscall2(SYS_munmap, (int64_t)addr, (int64_t)len);
}

//==============================================================================
// 内核态：系统调用处理函数原型
//==============================================================================

// 每个 syscall 的内核实现，返回 0 表示成功，负数表示错误码
// 这些函数由 core/syscall.c 实现，通过 do_syscall 分发

int64_t kern_exit(int code);
int64_t kern_write(int fd, const void *buf, size_t len);
int64_t kern_read(int fd, void *buf, size_t len);
int     kern_open(const char *path, int flags);
int     kern_close(int fd);
int     kern_sleep(unsigned ms);
int     kern_getpid(void);
int64_t kern_yield(void);
void   *kern_mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t off);
int     kern_munmap(void *addr, size_t len);

// 分发函数，由 arch 入口调用
// num = %rax（syscall 编号），a1–a6 = %rdi, %rsi, %rdx, %r10, %r8, %r9
int64_t do_syscall(int64_t num,
        int64_t a1, int64_t a2, int64_t a3,
        int64_t a4, int64_t a5, int64_t a6);

#endif // SYSCALL_H
