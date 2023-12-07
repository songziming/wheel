// 中断和异常处理

#include <arch_int.h>
#include <wheel.h>
#include <arch_api_p.h>
#include <arch_cpu.h>
#include <arch_mem.h>


PCPU_DATA int g_int_depth = 0;
// PCPU_BSS uint64_t g_int_rsp;


//------------------------------------------------------------------------------
// 准备中断处理机制
//------------------------------------------------------------------------------

// mem/pcpu.c 已经分配了每个 CPU 的异常栈和中断栈

// 为每个 CPU 分配异常处理专用栈，并映射到内核虚拟地址
// TODO 需要让每个 CPU 的中断栈按 L1 对齐
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
}


//------------------------------------------------------------------------------
// 异常处理函数
//------------------------------------------------------------------------------

typedef struct exp_frame {
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rax;
    uint64_t errcode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED exp_frame_t;

void handle_exception(int vec, exp_frame_t *f) {
    klog("exception %d from rip=%lx, frame=%p\n", vec, f->rip, f);

    size_t rela;
    const char *name = sym_resolve(f->rip, &rela);
    klog("exception from %s + 0x%lx\n", name, rela);

    print_stacktrace();

    // 如果要返回，调整 rip 到后一条指令，跳过这个出错的 ud2

    cpu_halt();
}


//------------------------------------------------------------------------------
// 中断处理函数
//------------------------------------------------------------------------------

typedef struct int_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t errcode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED int_frame_t;

void handle_interrupt(int vec, int_frame_t *f) {
    klog("interrupt %d\n", vec);

    size_t frames[32];
    int depth = arch_unwind(frames, 32, f->rbp);
    print_frames(frames, depth);
}
