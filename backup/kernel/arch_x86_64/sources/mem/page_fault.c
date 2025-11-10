#include <arch_int.h>
#include <arch_api_p.h>
#include <wheel.h>
#include <cpu/rw.h>


// 处理分页异常，有自己的 IST

// 异常的 regs 是不完整的

void handle_pagefault(int vec UNUSED, arch_regs_t *f) {
    ASSERT(14 == vec);

    klog("#PF cpu-%d rip=%lx rsp=%lx\n", cpu_index(), f->rip, f->rsp);

    size_t frames[32];
    // int depth = arch_unwind_from(frames, 32, f->rbp);
    int depth = arch_unwind(frames, 32);
    print_frames(frames, depth);

    uint64_t va = read_cr2();

    const char *rw = (f->errcode & 2) ? "write" : "read";
    klog("error when %s %lx\n", rw, va);

    // if (0 == (f->errcode & 1)) {
    //     klog("page not exist\n");
    // }

    // TODO 找出发生错误的地址属于哪个任务，位于哪个地址空间
    //      进而找出所在 vmrange
    task_t *prev = THISCPU_GET(g_tid_prev);
    if (NULL != prev) {
        vmrange_t *rng = context_query(prev->process, va);
        if (NULL != rng) {
            klog("within task %s, range %s %lx~%lx\n",
                prev->name, rng->desc, rng->addr, rng->end);
        }
    }

    while (1) {
        cpu_halt();
    }
}
