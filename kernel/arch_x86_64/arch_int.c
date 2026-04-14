#include "arch_int.h"
#include "arch_api.h"
#include "cpu/gdt_idt_tss.h"
#include "mem/mem.h"
#include <debug.h>


// 中断栈栈顶，主动切换到这个位置
PERCPU_BSS int g_int_depth;
PERCPU_BSS size_t g_int_stack_top;

// 切换中断栈时，在这里保存原来的栈顶
PERCPU_BSS size_t g_curr_stack;

// 中断处理函数
int_handler_t irq_handlers[256];

// arch_entries.S
extern uint64_t isr_entries[256];
extern void syscall_entry();

// 默认的中断处理函数
static void handle_irq(int vec, regs_t *f) {
    logk("[cpu-%d] handling interrupt vec #%d\n", cpu_index(), vec);
    logk("rip=%zx rsp=%zx frame=%zx\n", f->rip, f->rsp, (size_t)f);

    if (14 == vec) {
        logk("cr2 = %zx\n", read_cr2());
    }

    // size_t sp;
    // ASMV("movq %%rsp, %0" : "=r"(sp));
    // logk("current stack pointer %zx\n", sp);

    // if (++irq_idx < 5) {
    //     return;
    // }

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

    for (int i = 0; i < cpu_count(); ++i) {
        tss_set_ist(i, 1, get_ist_nmi(i));
        tss_set_ist(i, 2, get_ist_df(i));
        tss_set_ist(i, 3, get_ist_pf(i));
        tss_set_ist(i, 4, get_ist_mc(i));

        *PERCPU(i, &g_int_depth) = 0;
        *PERCPU(i, &g_int_stack_top) = get_int_top(i);
    }

    idt_set_ist(2,  1); // NMI
    idt_set_ist(8,  2); // #DF
    idt_set_ist(14, 3); // #PF
    idt_set_ist(18, 4); // #MC


    // 设置系统调用相关 MSR（只允许 64-bit 模式下的系统调用入口）
    write_msr(MSR_STAR, 0x001b0008UL << 32);    // STAR
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry); // LSTAR
    write_msr(MSR_SFMASK, 0UL);                     // SFMASK

    // 测试用，#UD 也使用 NMI 的异常栈
    // idt_set_ist(6, 1);
}
