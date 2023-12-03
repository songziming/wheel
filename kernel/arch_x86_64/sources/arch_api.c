#include <arch_api_p.h>
#include <wheel.h>
#include <arch_mem.h>


//------------------------------------------------------------------------------
// 部分 ARCH-API 支持函数的实现
//------------------------------------------------------------------------------

inline void cpu_halt() { __asm__("hlt"); }
inline void cpu_pause() { __asm__("pause"); }
inline void cpu_rfence() { __asm__("lfence" ::: "memory"); }
inline void cpu_wfence() { __asm__("sfence" ::: "memory"); }
inline void cpu_rwfence() { __asm__("mfence" ::: "memory"); }

inline int cpu_int_lock() {
    uint64_t key;
    __asm__("pushfq; cli; popq %0" : "=r"(key));
    return (key & 0x200) ? 1 : 0;
}

inline void cpu_int_unlock(int key) {
    if (key) {
        __asm__("sti");
    }
}

// bochs magic breakpoint
void emu_break() {
    __asm__("xchgw %bx, %bx");
}

// 退出 QEMU/Bochs 模拟器并返回值
NORETURN void emu_exit(int ret) {
#ifdef DEBUG
    __asm__("outl %0, %1" :: "a"(ret), "Nd"(0xf4));
#else
    (void)ret;
#endif
    while (1) {
        cpu_halt();
    }
}


//------------------------------------------------------------------------------
// 堆栈展开
//------------------------------------------------------------------------------

// AMD64 栈结构（向下生长）：
// high |     arg 8     |
//      |     arg 7     |
//      |  return addr  | <- 刚跳转之后的 RSP
//      |    old RBP    | <- RBP
//      |  local var x  |
//  low |  local var y  | <- RSP
int arch_unwind(size_t *addrs, int max, uint64_t rbp) {
    int i = 0;

    for (; i < max; ++i) {
        uint64_t *frame = (uint64_t *)rbp;
        addrs[i] = (size_t)frame[1];
        if (0 == addrs[i]) {
            break;
        }
        rbp = frame[0];
    }

    return i;
}

// 获取当前调用栈，返回深度
int unwind(size_t *addrs, int max) {
    uint64_t rbp;
    __asm__("movq %%rbp, %0" : "=r"(rbp));
    return arch_unwind(addrs, max, rbp);
}
