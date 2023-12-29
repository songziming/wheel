// 中断和异常处理

#include <arch_int.h>
#include <wheel.h>
#include <arch_api_p.h>
#include <arch_cpu.h>
#include <arch_mem.h>


// 每个异常/中断的处理函数指针
void *g_handlers[256] = { NULL };

// 每个CPU的当前中断嵌套深度
PCPU_DATA int g_int_depth = 0;

// 需要切换到的中断栈地址
PCPU_DATA size_t g_int_stack;



//------------------------------------------------------------------------------
// 默认 handler
//------------------------------------------------------------------------------

// 通用异常处理
void handle_exception(int vec, arch_regs_t *f) {
    klog("exception %d from rip=%lx, frame=%p\n", vec, f->rip, f);

    size_t rela;
    const char *name = sym_resolve(f->rip, &rela);
    klog("from %s + 0x%lx\n", name, rela);

    cpu_halt();
}

// 通用中断处理
void handle_interrupt(int vec, arch_regs_t *f) {
    klog("interrupt %d\n", vec);

    size_t frames[32];
    int depth = arch_unwind(frames, 32, f->rbp);
    print_frames(frames, depth);

    cpu_halt();
}


//------------------------------------------------------------------------------
// 准备中断处理机制
//------------------------------------------------------------------------------

// mem/pcpu.c 已经分配了每个 CPU 的异常栈和中断栈

// TODO 我们在 local_apic.c 中，配置了只有 BSP 接收 NMI，因此只有 BSP 需要 NMI 栈
//      但是现在，为每个处理器都分配了相同大小的栈

void int_init() {
    ASSERT(NULL != g_range_pcpu_vars);
    ASSERT(NULL != g_range_pcpu_nmi);
    ASSERT(NULL != g_range_pcpu_df);
    ASSERT(NULL != g_range_pcpu_pf);
    ASSERT(NULL != g_range_pcpu_mc);
    ASSERT(NULL != g_range_pcpu_int);

    // 每个 CPU 可以设置最多 7 个 IST，编号 1~7
    // TSS 里面记录的是栈顶，也就是结束地址
    for (int i = 0; i < cpu_count(); ++i) {
        tss_set_ist(i, 1, g_range_pcpu_nmi[i].end);
        tss_set_ist(i, 2, g_range_pcpu_df[i].end);
        tss_set_ist(i, 3, g_range_pcpu_pf[i].end);
        tss_set_ist(i, 4, g_range_pcpu_mc[i].end);

        // 中断栈不使用 IST，因为 IST 不能重入，而中断可以
        *(size_t *)pcpu_ptr(i, &g_int_stack) = g_range_pcpu_int[i].end;
    }

    // IDT 是所有 CPU 共享的
    idt_set_ist(2,  1); // NMI
    idt_set_ist(8,  2); // #DF
    idt_set_ist(14, 3); // #PF
    idt_set_ist(18, 4); // #MC

    // 设置中断处理函数
    // TODO 填入几个重要异常的处理函数（PF），或者等相关模块自己设置
    for (int i = 0; i < 32; ++i) {
        g_handlers[i] = handle_exception;
    }
    for (int i = 32; i < 256; ++i) {
        g_handlers[i] = handle_interrupt;
    }
}

void set_int_handler(int vec, int_handler_t handler) {
    ASSERT(vec >= 0);
    ASSERT(vec < 256);
    g_handlers[vec] = handler;
}
