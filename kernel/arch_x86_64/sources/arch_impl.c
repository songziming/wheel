#include <arch_impl.h>
// #include <liba/rw.h>
// #include <arch_smp.h>


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
int arch_unwind(void **addrs, int max, uint64_t rbp) {
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
