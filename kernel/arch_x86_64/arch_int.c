#include <wheel.h>
#include <arch_api.h>
#include <debug.h>


// 中断栈上下文
// 需要和 arch_entries.S 里面寄存器压栈、出栈的顺序匹配
typedef struct regs {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    // 上面的寄存器在中断时、系统调用时才会保存
    // 下面的寄存器在所有情况下都会保存
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rax;

    // 下面是中断异常发生时自动入栈的内容
    uint64_t errcode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} regs_t;


static int irq_idx = 0;

void handle_irq(int vec, regs_t *f) {
    logk("handling interrupt vec 0x%x\n", vec);
    logk("rip=%lx rsp=%lx\n", f->rip, f->rsp);

    if (++irq_idx < 5) {
        return;
    }

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}
