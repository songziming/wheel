// #include <common.h>
#include "sched.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <library/dllist.h>
#include <library/string.h>
#include <library/debug.h>
#include <library/spin.h>



// 按优先级排序的有序队列，可用于就绪队列和阻塞队列
typedef struct priority_q {
    uint32_t    priorities; // 优先级mask
    dlnode_t   *heads[32];
    spin_t      spin;
} priority_q_t;


PCPU_BSS priority_q_t g_ready_q;

PCPU_BSS task_t *g_tid_prev;
PCPU_BSS task_t *g_tid_next;


//------------------------------------------------------------------------------
// 任务队列
//------------------------------------------------------------------------------

// 可用于就绪队列、阻塞队列

static void priority_q_init(priority_q_t *q) {
    q->priorities = 0;
    memset(q->heads, 0, sizeof(q->heads));
}

static void priority_q_push(priority_q_t *q, task_t *tid) {
    ASSERT(NULL != q);
    ASSERT(NULL != tid);
    int pri = tid->priority;
    if ((1U << pri) & q->priorities) {
        dl_insert_before(&tid->q_node, q->heads[pri]);
    } else {
        dl_init_circular(&tid->q_node);
        q->heads[pri] = &tid->q_node;
        q->priorities |= 1U << pri;
    }
}

static int priority_q_contains(priority_q_t *q, task_t *tid) {
    ASSERT(NULL != q);
    ASSERT(NULL != tid);
    int pri = tid->priority;
    if ((1U << pri) & q->priorities) {
        return dl_contains(q->heads[pri], &tid->q_node);
    }
    return 0;
}

static task_t *priority_q_head(priority_q_t *q) {
    ASSERT(NULL != q);
    if (0 == q->priorities) {
        return NULL;
    }
    int top = __builtin_ctz(q->priorities);
    ASSERT(NULL != q->heads[top]);
    return containerof(q->heads[top], task_t, q_node);
}

static void priority_q_remove(priority_q_t *q, task_t *tid) {
    ASSERT(NULL != q);
    ASSERT(NULL != tid);

    int pri = tid->priority;
    ASSERT((1U << pri) & q->priorities);
    ASSERT(dl_contains(q->heads[pri], &tid->q_node));

    if (dl_is_lastone(&tid->q_node)) {
        ASSERT(q->heads[pri] == &tid->q_node);
        q->heads[pri] = NULL;
        q->priorities &= ~(1U << pri);
        return;
    }

    dlnode_t *next = dl_remove(&tid->q_node);
    if (q->heads[pri] == &tid->q_node) {
        q->heads[pri] = next;
    }
}

// 遍历每个 CPU 的就绪队列，找出优先级最低的 CPU
static int lowest_cpu() {
    for (int i = 0; i < cpu_count(); ++i) {
        priority_q_t *q = percpu_ptr(i, &g_ready_q);
    }
    return 0;
}



// 执行调度，在时钟中断里执行
void sched_advance() {
    ASSERT(cpu_int_depth());

    priority_q_t *q = thiscpu_ptr(&g_ready_q);

    // 锁住当前队列，防止 tid_next 改变
}


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

INIT_TEXT void sched_init() {
    for (int i = 0, N = cpu_count(); i < N; ++i) {
        priority_q_t *q = percpu_ptr(i, &g_ready_q);
        spin_init(&q->spin);
        priority_q_init(q);
    }
}
