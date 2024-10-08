#include "arch_task.h"
#include "arch_impl.h"
#include "arch_int.h"
#include "apic/loapic.h"

#include <proc/tick.h>
#include <proc/work.h>
#include <proc/sched.h>

#include <library/string.h>


// arch_entries.S
void task_entry();


// 创建新任务，初始化 TCB
void arch_regs_init(task_t *tid, size_t entry, void *arg1, void *arg2, void *arg3, void *arg4) {
    size_t top = tid->stack.end & ~7UL; // 栈顶按 8 字节对齐

    regs_t *regs = (regs_t *)(top - sizeof(regs_t));
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

static void on_resched(int vec UNUSED, regs_t *f UNUSED) {
    loapic_send_eoi();
}

// static void on_stopall(int vec UNUSED, regs_t *f UNUSED) {
//     // __asm__("cli");
//     cpu_int_lock();
//     while (1) {
//         cpu_halt();
//     }
// }

// 发送 IPI，通知另一个 CPU 切换任务
void arch_ipi_resched(int cpu) {
    loapic_send_ipi(cpu, VEC_IPI_RESCHED);
}

void notify_resched(cpuset_t cpus) {
    // TODO 将 resched 中断发送给多个 CPU
    // 本函数可以放在 sched.c 模块，遍历 cpuset，逐一调用 arch_ipi_resched
}

INIT_TEXT void arch_task_lib_init() {
    set_int_handler(VEC_IPI_RESCHED, on_resched);
    // set_int_handler(VEC_IPI_STOPALL, on_stopall);

    tick_init();
    work_init();
    sched_init();
}
