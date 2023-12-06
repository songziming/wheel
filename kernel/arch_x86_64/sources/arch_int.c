// 中断和异常处理

#include <arch_int.h>
#include <wheel.h>
#include <arch_api_p.h>
#include <arch_cpu.h>



// TODO 中断栈应该使用 page-alloc 动态申请，再映射到内存空间
//      中断栈之前保留 guard page，这样可以检测到栈溢出
// 也可以将多占用一个页，将低地址页释放，标记为无效，当作 guard-page
// 如果预留连续内存，再释放其中一个页，会破坏内核地址空间连续的页表项
// 导致内核无法统一使用 2M 页表项
PCPU_BSS uint8_t  g_int_stack[INT_STACK_SIZE+PAGE_SIZE] ALIGNED(PAGE_SIZE);


PCPU_BSS int      g_int_depth;
PCPU_BSS uint64_t g_int_rsp;


//------------------------------------------------------------------------------
// 准备中断处理机制
//------------------------------------------------------------------------------

// 为每个 CPU 分配异常处理专用栈，并映射到内核虚拟地址
// TODO 需要让每个 CPU 的中断栈按 L1 对齐
void int_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        *(int *)pcpu_ptr(i, &g_int_depth) = 0;
        *(uint64_t *)pcpu_ptr(i, &g_int_rsp) = (uint64_t)pcpu_ptr(i, &g_int_stack[INT_STACK_SIZE]);
    }
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
    klog("frame addr %p\n", f);
    klog("exception %d from rip=%lx\n", vec, f->rip);

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
