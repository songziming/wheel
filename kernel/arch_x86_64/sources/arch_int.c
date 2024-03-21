// 中断和异常处理

#include <arch_int.h>
#include <arch_api_p.h>
#include <arch_mem.h>
#include <cpu/gdt_idt_tss.h>
#include <wheel.h>


// 每个异常/中断的处理函数指针
void *g_handlers[256] = { NULL };

// 每个CPU的当前中断嵌套深度
PCPU_BSS int g_int_depth;

// 需要切换到的中断栈地址
PCPU_BSS size_t g_int_stack;



//------------------------------------------------------------------------------
// 默认 handler
//------------------------------------------------------------------------------

// 通用异常处理
void handle_exception(int vec, arch_regs_t *f) {
    klog("[CPU%d] exception %d from frame=%p, rip=%lx, rsp=%lx\n",
            cpu_index(), vec, f, f->rip, f->rsp);

    size_t rela;
    const char *name = sym_resolve(f->rip, &rela);
    klog("from %s + 0x%lx\n", name, rela);

    cpu_halt();
}

// 通用中断处理
void handle_interrupt(int vec, arch_regs_t *f) {
    klog("interrupt %d\n", vec);

    size_t frames[32];
    int depth = arch_unwind_from(frames, 32, f->rbp);
    print_frames(frames, depth);

    cpu_halt();
}


//------------------------------------------------------------------------------
// 准备中断处理机制
//------------------------------------------------------------------------------

// mem/pcpu.c 已经分配了每个 CPU 的异常栈和中断栈

void int_init() {
    ASSERT(0 == cpu_index());

    // 每个 CPU 可以设置最多 7 个 IST，编号 1~7
    // TSS 里面记录的是栈顶，也就是结束地址
    for (int i = 0; i < cpu_count(); ++i) {
        vmrange_t *nmi = pcpu_ptr(i, &g_range_pcpu_nmi);
        vmrange_t *df  = pcpu_ptr(i, &g_range_pcpu_df);
        vmrange_t *pf  = pcpu_ptr(i, &g_range_pcpu_pf);
        vmrange_t *mc  = pcpu_ptr(i, &g_range_pcpu_mc);

        tss_set_ist(i, 1, nmi->end);
        tss_set_ist(i, 2, df->end);
        tss_set_ist(i, 3, pf->end);
        tss_set_ist(i, 4, mc->end);

        *(int *)pcpu_ptr(i, &g_int_depth) = 0;

        // 中断栈不使用 IST，因为 IST 不能重入，而中断可以
        vmrange_t *intstack = pcpu_ptr(i, &g_range_pcpu_int);
        *(size_t *)pcpu_ptr(i, &g_int_stack) = intstack->end;
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
    ASSERT(NULL != handler);
    g_handlers[vec] = handler;
}

inline int cpu_int_depth() {
    return THISCPU_GET(g_int_depth);
}
