// 平台相关的调试辅助函数
// 实现 arch_api.h 中规定的函数（部分）
// 还有一些接口函数在其他模块里实现

// 开启了 LTO，即使处于不同 TU 也能内联


#include <arch_api_p.h>
#include <liba/rw.h>
#include <arch_smp.h>


inline void cpu_halt() { __asm__("hlt"); }
inline void cpu_pause() { __asm__("pause"); }
inline void cpu_rfence() { __asm__("lfence" ::: "memory"); }
inline void cpu_wfence() { __asm__("sfence" ::: "memory"); }
inline void cpu_rwfence() { __asm__("mfence" ::: "memory"); }


// AMD64 栈结构（向下生长）：
// high |     arg 8     |
//      |     arg 7     |
//      |  return addr  | <- 刚跳转之后的 RSP
//      |    old RBP    | <- RBP
//      |  local var x  |
//  low |  local var y  | <- RSP
int unwind_from(void **addrs, int max, uint64_t rbp) {
    int i = 0;

    for (; i < max; ++i) {
        uint64_t *frame = (uint64_t *)rbp;
        addrs[i] = (void *)frame[1];
        if (NULL == addrs[i]) {
            break;
        }
        rbp = frame[0];
    }

    return i;
}

// 获取当前调用栈，返回深度
int unwind(void **addrs, int max) {
    uint64_t rbp;
    __asm__("movq %%rbp, %0" : "=r"(rbp));
    return unwind_from(addrs, max, rbp);
}

// 退出 QEMU/Bochs 模拟器并返回值
void emulator_exit(int ret) {
#ifdef DEBUG
    __asm__("outl %0, %1" :: "a"(ret), "Nd"(0xf4));
#else
    (void)ret;
#endif
}

inline int cpu_count() {
    return g_loapic_num;
}

inline int cpu_index() {
    int idx;
    __asm__("movl %%gs:(g_cpu_index), %0" : "=a"(idx));
    return idx;
}
