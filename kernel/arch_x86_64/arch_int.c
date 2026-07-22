#include "arch_int.h"
#include "arch_api.h"
#include "cpu/gdt_idt_tss.h"
#include "mem/mem.h"
#include <debug.h>


// 中断栈栈顶，主动切换到这个位置
PERCPU_BSS int g_int_depth;
PERCPU_BSS size_t g_int_stack_top;

// 中断处理函数
int_handler_t irq_handlers[256];

// arch_entries.S
extern uint64_t isr_entries[256];
extern void syscall_entry();



inline int cpu_int_depth() {
    return THISCPU_GET(g_int_depth);
}

inline int cpu_int_disable() {
    uint64_t key;
    ASMV("pushfq; cli; popq %0" : "=r"(key));
    return (key & 0x200) ? 1 : 0;
}

inline void cpu_int_restore(int key) {
    if (key) {
        ASMV("sti");
    }
}


// 默认的中断处理函数
// TODO 中断里面不应该打印，可能得不到 serial 自旋锁
static void handle_irq(int vec, regs_t *f) {
    logk("[cpu-%d] interrupt vec #%d err=%zx\n", cpu_index(), vec, f->errcode);
    logk("rip=%zx rsp=%zx rbp=%zx frame=%p\n", f->rip, f->rsp, f->rbp, f);

    size_t frames[32];
    int depth;
    if (vec < 32) {
        // 如果是 exceeption，则不会保存 callee-save-regs
        // 中断栈是连续的，直接在这里 unwing 就能看到异常位置
        depth = arch_unwind(frames, 32);
    } else {
        depth = arch_unwind_from(frames, 32, f->rbp);
    }
    logk("backtrace (%d):\n", depth);
    for (int i = 0; i < depth; ++i) {
        logk(" - frame[%02d] 0x%zx\n", i, frames[i]);
    }

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// page fault 处理函数
static void handle_pf(int vec UNUSED, regs_t *f) {
    uint64_t va = read_cr2();
    const char *p  = (f->errcode & 1) ? "" : "non-";
    const char *wr = (f->errcode & 2) ? "write to" : "read from";
    const char *us = (f->errcode & 4) ? "user mode" : "kernel";
    if (f->errcode & 16) {
        wr = "execute";
    }
    logk("[cpu-%d] #PF %s %spresent addr 0x%zx from %s\n",
        cpu_index(), wr, p, va, us);

    if (f->errcode & 8) { // 页表项中的保留位必须是 0
        logk("page-entry reserved bit was set!!\n");
    }

    vmrange_t *rng = vmspace_lookup(&g_kernel_vm, va);
    if (rng) {
        logk("trying to access range %s 0x%zx~0x%zx\n",
            rng->desc, rng->vaddr, rng->vend);
    } else {
        rng = vmspace_lookup(&g_kernel_vm, va + PAGE_SIZE);
        if (rng) {
            logk("right before range %s 0x%zx~0x%zx\n",
                rng->desc, rng->vaddr, rng->vend);
        } else {
            logk("cannot locate vmrange\n");
        }
    }

    logk("rip=%zx rsp=%zx rbp=%zx\n", f->rip, f->rsp, f->rbp);
    size_t frames[32];
    // int depth = arch_unwind_from(frames, 32, f->rbp);
    int depth = arch_unwind(frames, 32);
    logk("backtrace (%d):\n", depth);
    for (int i = 0; i < depth; ++i) {
        logk(" - frame[%02d] 0x%zx\n", i, frames[i]);
    }

    // TODO 如果 page fault 来自任务，可以将引发异常的任务停止（kill）
    // 从 ready-q 删除任务，便可以从异常返回（到其他任务），不必在这里死循环

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 每个 cpu 都要执行此函数
// 在 TSS 中设置 IST，在 IDT 里面填入 IST-idx
INIT_TEXT void int_init() {
    for (int i = 0; i < 256; ++i) {
        idt_set_isr(i, isr_entries[i], 0);
        irq_handlers[i] = handle_irq;
    }
    irq_handlers[14] = handle_pf;

    // // 0x80 可以用于系统调用
    // idt_set_isr(0x80, isr_entries[0x80], 3);
    // irq_handlers[0x80] = handle_syscall_80;

    idt_set_ist(2,  1); // NMI
    idt_set_ist(8,  2); // #DF
    idt_set_ist(14, 3); // #PF
    idt_set_ist(18, 4); // #MC
}

INIT_TEXT void int_init_local() {
    // 需要和 IDT 使用的 IST 编号匹配
    thistss_set_ist(1, thiscpu_nmi_stack());
    thistss_set_ist(2, thiscpu_df_stack());
    thistss_set_ist(3, thiscpu_pf_stack());
    thistss_set_ist(4, thiscpu_mc_stack());

    THISCPU_SET(g_int_depth, 0);
    THISCPU_SET(g_int_stack_top, thiscpu_int_stack());

    // 设置系统调用相关 MSR（只允许 64-bit 模式下的系统调用入口）
    // syscall CS = STAR[47:32]
    // syscall SS = STAR[47:32] + 8
    // sysret CS = STAR[63:48] + 16
    // sysret SS = STAR[63:48] + 8
    write_msr(MSR_EFER, read_msr(MSR_EFER) | 1);    // enable syscall
    write_msr(MSR_STAR, 0x001b0008UL << 32);        // STAR
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);  // LSTAR
    write_msr(MSR_SFMASK, 0x200UL); // clear IF
}
