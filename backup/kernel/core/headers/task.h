#ifndef TASK_H
#define TASK_H

#include <def.h>
#include <arch_extra.h>
#include <spin.h>
// #include <process.h>
#include <tick.h>
#include <context.h>


// 任务状态，多个状态可以并存
typedef enum task_state {
    TASK_READY   = 0,
    TASK_PENDING = 1,   // 因为 semaphore、pipe、msgqueue 等资源而阻塞，处于它们的等待队列中
    TASK_STOPPED = 2,   // 被手动暂停，不在就绪队列中
    TASK_DELETED = 4,   // 已停止，即将删除（已预约下一次 work_queue）
} task_state_t;


typedef struct task {
    arch_tcb_t   arch;
    spin_t       spin;
    const char  *name;

    context_t   *process;   // 所属进程（地址空间）
    void        *stack;     // NULL 表示任务栈是静态分配的

    uint16_t     state;
    uint8_t      priority;  // 数字越小，优先级越高
    int          affinity;  // 负值表示不限制在哪个 CPU 上运行
    int          last_cpu;  // 最近一次运行在哪个 CPU 之上，位于哪个就绪队列之中
    uint32_t     tick_reload;
    uint32_t     tick;
    dlnode_t     q_node;    // 任务在就绪队列或阻塞队列中的节点
} task_t;


int task_create_ex(task_t *task, const char *name,
        uint8_t priority, int affinity, context_t *proc,
        void *stack_top, uint8_t stack_rank,
        void *entry, size_t a1, size_t a2, size_t a3, size_t a4);

int task_create(task_t *task, const char *name, uint8_t priority, void *entry);

void task_resume(task_t *task);
void task_delay(int ticks);
void task_exit();


#endif // TASK_H
