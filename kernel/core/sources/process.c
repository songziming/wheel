#include <process.h>
#include <wheel.h>


// 记录内核进程，作用有些冗余
static process_t g_kernel_proc = {
    .table = INVALID_ADDR,
};

void process_create(process_t *proc) {
    ASSERT(NULL != proc);

    vmspace_init(&proc->space);
    proc->table = INVALID_ADDR; // lazy init，真正用到用户态地址时，再初始化相关字段
    dl_init_circular(&g_kernel_proc.proc_head);
}

process_t *get_kernel_process() {
    if (INVALID_ADDR == g_kernel_proc.table) {
        process_create(&g_kernel_proc);
        g_kernel_proc.table = get_kernel_pgtable();
    }
    return &g_kernel_proc;
}
