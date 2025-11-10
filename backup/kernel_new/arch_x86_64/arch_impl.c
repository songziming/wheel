#include <arch_intf.h>
#include <arch_int.h>
#include <proc/sched.h>
#include <library/string.h>


// 内联汇编，实现 arch 接口
// 大部分 arch_api 实现分散在各个 arch 文件里，此处只是一部分


void emu_exit(int ret) {
#ifdef DEBUG
    __asm__("outl %0, %1" :: "a"(ret), "Nd"(0xf4));
#else
    (void)ret;
#endif
}

void emu_break() {
    __asm__("xchgw %bx, %bx");
}

inline void cpu_halt() { __asm__("hlt"); }
inline void cpu_pause() { __asm__("pause"); }
inline void cpu_rfence() { __asm__ volatile("lfence" ::: "memory"); }
inline void cpu_wfence() { __asm__ volatile("sfence" ::: "memory"); }
inline void cpu_rwfence() { __asm__ volatile("mfence" ::: "memory"); }


// AMD64 栈结构（向下生长）：
// high |     arg 8     |
//      |     arg 7     |
//      |  return addr  | <- 刚跳转之后的 RSP
//      |    old RBP    | <- RBP
//      |  local var x  |
//  low |  local var y  | <- RSP
int arch_unwind_from(size_t *addrs, int max, uint64_t rbp) {
    int i = 0;

    for (; (i < max) && (0 != rbp); ++i) {
        uint64_t *frame = (uint64_t*)rbp;
        addrs[i] = (size_t)frame[1];
        if (0 == addrs[i]) {
            break;
        }
        rbp = frame[0];
    }

    return i;
}

// 获取当前调用栈，返回深度
int arch_unwind(size_t *addrs, int max) {
    uint64_t rbp;
    __asm__("movq %%rbp, %0" : "=r"(rbp));
    return arch_unwind_from(addrs, max, rbp);
}




// arch_entries.S
void task_entry();


// 创建新任务，初始化 TCB
void arch_task_init(task_t *tid, size_t entry, void *arg1, void *arg2, void *arg3, void *arg4) {
    size_t top = tid->stack.end & ~7UL; // 栈顶按 8 字节对齐

    regs_t *regs = (regs_t*)(top - sizeof(regs_t));
    memset(regs, 0, sizeof(regs_t));

    regs->cs = 0x08;
    regs->ss = 0x10;
    regs->rflags = 0x0200UL;    // 开启中断
    regs->rsp = top;
    regs->rip = (uint64_t)task_entry;
    regs->rax = (uint64_t)entry; // 通过 rax 传入真正的入口函数地址
    regs->rdi = (uint64_t)arg1;
    regs->rsi = (uint64_t)arg2;
    regs->rdx = (uint64_t)arg3;
    regs->rcx = (uint64_t)arg4;

    tid->arch_regs = regs;
}
