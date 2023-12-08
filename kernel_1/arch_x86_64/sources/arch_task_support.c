#include <arch_interface.h>
#include <arch_int.h>

#include <debug.h>
#include <libk.h>


// 创建一个新任务
// entry 对应的函数不能返回
void task_regs_init(task_regs_t *regs, uint8_t stack[], size_t size, void *entry) {
    uint64_t top = ((uint64_t)stack + size - 16) & ~15; // 留出返回地址的位置
    uint64_t rsp = top - sizeof(int_context_t); // 中断发生后的 rsp
    ASSERT(rsp >= (uint64_t)stack);

    // 写入返回地址（零）
    *(uint64_t *)top = 0;

    // 准备任务栈，伪装刚刚发生中断的假象
    int_context_t *ctx = (int_context_t *)rsp;
    memset(ctx, 0, sizeof(int_context_t));
    ctx->cs  = 0x08;
    ctx->ss  = 0x10;
    ctx->rip = (uint64_t)entry;
    ctx->rsp = top;
    ctx->rflags = 0x202; // 开启中断
    ctx->err = (uint64_t)-1;

    // 将中断后的栈指针记录在 TCB 中
    memset(regs, 0, sizeof(task_regs_t));
    regs->stack_top = ctx;
}
