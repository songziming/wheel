// 中断和异常处理

#include <arch_int.h>
#include <wheel.h>
#include <arch_api_p.h>
#include <arch_cpu.h>
#include <arch_mem.h>



PCPU_DATA int g_int_depth = 0;

void *g_handlers[256] = { NULL };



//------------------------------------------------------------------------------
// 默认 handler
//------------------------------------------------------------------------------

// 通用异常处理
void handle_exception(int vec, exp_frame_t *f) {
    klog("exception %d from rip=%lx, frame=%p\n", vec, f->rip, f);

    size_t rela;
    const char *name = sym_resolve(f->rip, &rela);
    klog("exception from %s + 0x%lx\n", name, rela);

    print_stacktrace();

    // 如果要返回，调整 rip 到后一条指令，跳过这个出错的 ud2

    cpu_halt();
}

// 通用中断处理
void handle_interrupt(int vec, int_frame_t *f) {
    klog("interrupt %d\n", vec);

    size_t frames[32];
    int depth = arch_unwind(frames, 32, f->rbp);
    print_frames(frames, depth);
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
        tss_set_ist(i, 5, g_range_pcpu_int[i].end);
    }

    // IDT 是所有 CPU 共享的
    idt_set_ist(2,  1); // NMI
    idt_set_ist(8,  2); // #DF
    idt_set_ist(14, 3); // #PF
    idt_set_ist(18, 4); // #MC

    // 设置中断处理函数
    for (int i = 0; i < 32; ++i) {
        g_handlers[i] = handle_exception;
    }
    for (int i = 32; i < 256; ++i) {
        g_handlers[i] = handle_interrupt;
    }
}

void set_exp_handler(int vec, exp_handler_t handler) {
    ASSERT(vec >= 0);
    ASSERT(vec < 32);
    g_handlers[vec] = handler;
}

void set_int_handler(int vec, int_handler_t handler) {
    ASSERT(vec >= 32);
    ASSERT(vec < 256);
    g_handlers[vec] = handler;
}
