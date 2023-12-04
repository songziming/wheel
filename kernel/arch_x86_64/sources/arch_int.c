// 中断和异常处理

#include <arch_int.h>
#include <wheel.h>
#include <arch_api_p.h>
#include <arch_cpu.h>



// TODO 中断栈应该使用 page-alloc 动态申请，再映射到内存空间
//      中断栈之前保留 guard page，这样可以检测到栈溢出
PCPU_BSS uint8_t int_stack[INT_STACK_SIZE]; // 每个 CPU 独享的中断栈
PCPU_DATA int g_int_depth = 0;


//------------------------------------------------------------------------------
// 准备中断处理机制
//------------------------------------------------------------------------------

// void int_init() {
//     tss_set_rsp
// }


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
