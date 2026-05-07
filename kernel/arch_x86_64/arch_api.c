#include "arch_api.h"
#include "mem/mem.h"
#include "arch_int.h"

#include <task.h>
#include <kstring.h>


//------------------------------------------------------------------------------
// 多任务支持
//------------------------------------------------------------------------------

// AMD64 栈结构（向下生长）：
// high |     arg 8     |
//      |     arg 7     |
//      |  return addr  | <- 刚跳转之后的 RSP
//      |    old RBP    | <- RBP
//      |  local var x  |
//  low |  local var y  | <- RSP
int arch_unwind_from(size_t *addrs, int max, uint64_t rbp) {
    int i = 0;

    for (; (i < max) && (0 != rbp); ++i) {
        uint64_t *frame = (uint64_t*)rbp;
        addrs[i] = (size_t)frame[1];
        if (0 == addrs[i]) {
            break;
        }
        rbp = frame[0];
    }

    return i;
}

// 获取当前调用栈，返回深度
int arch_unwind(size_t *addrs, int max) {
    uint64_t rbp;
    ASMV("movq %%rbp, %0" : "=r"(rbp));
    return arch_unwind_from(addrs, max, rbp);
}


void arch_task_entry(); // defined in arch_entries.S

// 填写栈顶的寄存器
void arch_task_init(task_t *task, size_t entry, size_t stack_top) {
    stack_top &= ~15UL;  // 栈顶按 16 字节对齐

    // 写入 return addr，防止任务中 backtrace 越界
    stack_top -= 16;
    *(uint64_t*)stack_top = 0ULL;

    regs_t *regs = (regs_t*)(stack_top - sizeof(regs_t));
    kmemset(regs, 0, sizeof(regs_t));

    regs->cs = 0x08;
    regs->ss = 0x10;
    regs->rflags = 0x0200UL;    // 开启中断
    regs->rsp = stack_top;
    regs->rip = (uint64_t)arch_task_entry;
    // regs->rip = (uint64_t)entry;
    regs->rdi = (uint64_t)task;
    regs->rsi = (uint64_t)entry;

    task->regs = regs;
}
