#include <arch_int.h>
#include <arch_api_p.h>
#include <wheel.h>


// 处理分页异常，有自己的 IST

void handle_pagefault(int vec, arch_regs_t *f) {
    ASSERT(14 == vec);

    klog("#PF rip=%lx rsp=%lx rbp=%lx\n", f->rip, f->rsp, f->rbp);

    size_t frames[32];
    int depth = arch_unwind(frames, 32, f->rbp);
    print_frames(frames, depth);

    // TODO 找出发生错误的地址属于哪个任务

    while (1) {
        cpu_halt();
    }
}
