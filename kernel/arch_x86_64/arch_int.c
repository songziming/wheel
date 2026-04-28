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

inline int cpu_int_lock() {
    uint64_t key;
    ASMV("pushfq; cli; popq %0" : "=r"(key));
    return (key & 0x200) ? 1 : 0;
}

inline void cpu_int_unlock(int key) {
    if (key) {
        ASMV("sti");
    }
}


// 默认的中断处理函数
static void handle_irq(int vec, regs_t *f) {
    logk("[cpu-%d] interrupt vec #%d err=%zx\n", cpu_index(), vec, f->errcode);
    logk("rip=%zx rsp=%zx frame=%p\n", f->rip, f->rsp, f);

    if (14 == vec) {
        logk("cr2=%zx\n", read_cr2());
    }

    size_t frames[32];
    int depth = arch_unwind_from(frames, 32, f->rbp);
    logk("backtrace (%d):\n", depth);
    for (int i = 0; i < depth; ++i) {
        logk(" - frame[%02d] 0x%zx\n", i, frames[i]);
    }

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

    // for (int i = 0; i < cpu_count(); ++i) {
    //     thistss_set_ist(i, 1, thiscpu_ist_nmi(i));
    //     thistss_set_ist(i, 2, thiscpu_ist_df(i));
    //     thistss_set_ist(i, 3, thiscpu_ist_pf(i));
    //     thistss_set_ist(i, 4, thiscpu_ist_mc(i));
    //     *PERCPU(i, &g_int_depth) = 0;
    //     *PERCPU(i, &g_int_stack_top) = thiscpu_int_stack(i);
    // }

    idt_set_ist(2,  1); // NMI
    idt_set_ist(8,  2); // #DF
    idt_set_ist(14, 3); // #PF
    idt_set_ist(18, 4); // #MC
}

INIT_TEXT void thiscpu_int_init() {
    // 需要和 IDT 使用的 IST 编号匹配
    thistss_set_ist(1, thiscpu_nmi_stack());
    thistss_set_ist(2, thiscpu_df_stack());
    thistss_set_ist(3, thiscpu_pf_stack());
    thistss_set_ist(4, thiscpu_mc_stack());

    THISCPU_SET(g_int_depth, 0);
    THISCPU_SET(g_int_stack_top, thiscpu_int_stack());

    // 设置系统调用相关 MSR（只允许 64-bit 模式下的系统调用入口）
    write_msr(MSR_STAR, 0x001b0008UL << 32);    // STAR
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry); // LSTAR
    write_msr(MSR_SFMASK, 0UL);
}
