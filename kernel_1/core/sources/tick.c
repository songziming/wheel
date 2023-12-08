// 时钟中断处理函数
// 该文件修改为 sched.c 时钟中断只是调度的一个方面

#include <tick.h>
#include <debug.h>

#include <page.h>
#include <libk.h>
#include <dllist.h>


// “任务调度”就是决定 tid_next 是什么
// “任务切换”才是真的切换到 tid_next
PCPU_DATA task_t *g_tid_curr = NULL;   // 表示当前 CPU 正在运行的任务
PCPU_DATA task_t *g_tid_next = NULL;   // 表示当前 CPU 即将运行的任务


//------------------------------------------------------------------------------
// 任务控制
//------------------------------------------------------------------------------

// TODO 创建的一定是内核任务，用户态任务也是从内核任务开始的
//      内核任务一定使用 g_kernel_vm 与 g_kernel_map，这两个参数可以省去

// 分配栈空间并映射，初始化任务
// 但不会立即运行
void task_init(task_t *task, void *entry, size_t stack_size, vmspace_t *vm, size_t tbl) {
    stack_size +=   PAGE_SIZE - 1;
    stack_size &= ~(PAGE_SIZE - 1);

    task->stack_range.desc = "task stack";
    size_t va = vmspace_reserve(vm, stack_size, &task->stack_range,
            DYNAMIC_MAP_ADDR, DYNAMIC_MAP_END, RF_GUARD|RF_DOWN);
    if (INVALID_ADDR == va) {
        return;
    }

    uint8_t *stack = (uint8_t *)va;
    pfn_t stack_pages = (pfn_t)(stack_size >> PAGE_SHIFT);
    task->stack_pages = PAGE_LIST_EMPTY;

    // 逐页分配并映射
    // 新任务不一定会在当前 CPU 运行，因此不适合用 page-cache 分配
    for (pfn_t i = 0; i < stack_pages; ++i, va += PAGE_SIZE) {
        pfn_t pfn = page_block_alloc(0, PT_STACK);
        size_t pa = (size_t)pfn << PAGE_SHIFT;

        page_list_push_tail(&task->stack_pages, pfn);
        mmu_map(tbl, va, pa, 1, PAGE_WRITE);
    }

    // 初始化栈内容和寄存器
    task_regs_init(&task->regs, stack, task->stack_range.size, entry);
}


void task_exit(task_t *task) {
    // 该函数不能再当前任务内运行
    if ((0 == get_int_depth()) && (task == THISCPU_GET(g_tid_curr))) {
        // 不能删除正在运行的任务的 TCB
        // 将这个任务添加到 job-queue，下一次中断退出流程自动运行
        return;
    }

    // TODO 如果处于 ready 状态，需要从 ready-queue 移除

    // page_block_free(task->stack_range.pa, TASK_STACK_RANK);
}


//------------------------------------------------------------------------------
// 调度
//------------------------------------------------------------------------------

// static PCPU_DATA dlnode_t g_affinity_ready_q;
// static          dlnode_t g_global_ready_q;

// static PCPU_DATA uint64_t g_affinity_priorities;
// static          uint64_t g_global_priorities;

// 在时钟中断函数里调用，判断接下来执行哪个 task
void sched_main() {
    task_t *task = THISCPU_GET(g_tid_curr);
    --task->tick_remain;

    if (0 == task->tick_remain) {
        dlnode_t *node = task->dl.next;
        task = containerof(node, task_t, dl);
        task->tick_remain = task->tick_total;
        THISCPU_SET(g_tid_next, task);
    }
}


//------------------------------------------------------------------------------
// 时钟函数
//------------------------------------------------------------------------------

static size_t g_tick = 0;

// 该函数在中断内部执行，如 APIC Timer
// 每个 CPU 都会收到该中断
void tick_advance() {
    if (0 == get_cpu_index()) {
        ++g_tick;
    }
}
