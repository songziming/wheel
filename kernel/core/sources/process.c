#include <process.h>
#include <wheel.h>

process_t g_kernel_proc = {
    .table = INVALID_ADDR,
};

void process_create(process_t *proc) {
    ASSERT(NULL != proc);

    vmspace_init(&proc->space);
    proc->table = mmu_create_process_table();
}
