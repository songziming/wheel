// 用户态程序

#include <stdint.h>

// stub.S
uint64_t syscall64(uint64_t rdi, uint64_t rsi);
uint64_t syscall80(uint64_t rdi, uint64_t rsi);

#define SYSCALL syscall64

void sys_print(const char *ptr);


void main() {
    sys_print("hello from ring3");
    SYSCALL(5, 0);
    SYSCALL(3, 0);
    SYSCALL(4, 0);
    SYSCALL(0, 1);
    while (1) {
        __asm__ volatile("pause");
    }
}

// void syscall(int no) {
//     __asm__ volatile("int $0x80" :: "a"(no));
// }

void sys_print(const char *ptr) {
    SYSCALL(123, (uint64_t)ptr);
}

// // 使用 AMD64 提供的指令发起系统调用
// // 比软中断更快，不需要访问 IDT，不需要寄存器压栈，仅改变特权级
// void syscall_stub() {
//     __asm__ volatile("syscall");
// }
