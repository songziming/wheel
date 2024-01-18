#include <arch_api_p.h>
#include <arch_mem.h>
#include <arch_int.h>
#include <cpu/local_apic.h>
#include <wheel.h>



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

    for (; (i < max) && (0 != rbp); ++i) {
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


//------------------------------------------------------------------------------
// 多任务支持
//------------------------------------------------------------------------------

// arch_entries.S
void task_entry();

void arch_tcb_init(arch_tcb_t *tcb, size_t entry, size_t stacktop, size_t args[4]) {
    ASSERT(NULL != tcb);
    ASSERT(0 != entry);
    ASSERT(0 != stacktop);

    stacktop &= ~7UL;   // 栈顶需要按 8 字节对齐

    tcb->rsp0 = stacktop;
    tcb->regs = (arch_regs_t *)(stacktop - sizeof(arch_regs_t));
    memset(tcb->regs, 0, sizeof(arch_regs_t));

    tcb->regs->cs     = 0x08UL;             // 内核数据段
    tcb->regs->ss     = 0x10UL;             // 内核代码段
    tcb->regs->rflags = 0x0200UL;           // 开启中断
    tcb->regs->rip    = (uint64_t)task_entry;
    tcb->regs->rsp    = (uint64_t)stacktop;
    tcb->regs->rax    = (uint64_t)entry;
    tcb->regs->rdi    = (uint64_t)args[0];
    tcb->regs->rsi    = (uint64_t)args[1];
    tcb->regs->rdx    = (uint64_t)args[2];
    tcb->regs->rcx    = (uint64_t)args[3];
}


// 向其他 CPU 发送 IPI，通知其执行调度操作


static void handle_resched(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
    local_apic_send_eoi();
    // 中断返回过程自动切换任务
}

void arch_notify_resched(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    ASSERT(cpu_index() != cpu);
    local_apic_send_ipi(cpu, VEC_IPI_RESCHED);
}

INIT_TEXT void install_sched_handlers() {
    set_int_handler(VEC_IPI_RESCHED, handle_resched);
}
