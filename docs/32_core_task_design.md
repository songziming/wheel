# 任务管理设计总结

本文记录 wheel 内核任务管理子系统的设计思路、关键决策和实现细节。

## 任务状态

```
TS_READY   = 0    // 就绪态，位于就绪队列中，可被调度
TS_STOPPED = 1    // 停止态，刚创建或被人为暂停
TS_PENDING = 2    // 阻塞态，位于某个同步对象的等待队列中
TS_DELETED = 3    // 已删除，TCB 可复用
```

状态是位掩码，可组合。从就绪队列移除时设置阻塞位；`task_cont` 清除对应位，全部清除后任务回到 TS_READY。

## 优先级队列 prioq

```c
typedef struct prioq {
    dlnode_t   *heads[32];     // 每级优先级一个循环链表
    uint32_t    priorities;    // 位掩码，加速查找最高优先级
} prioq_t;
```

核心设计决策：**prioq 是纯数据结构，不包含锁**。

理由：不同使用者对锁的粒度要求不同。
- 就绪队列：锁同时保护 `g_rdyq` + `g_tid_next`
- semaphore：锁同时保护 `waitq` + `count`
- msgq：锁同时保护 `readers` + `writers` + `fifo`

如果 prioq 自带锁，会迫使调用方用两把锁保护一组相关状态，引入不必要的锁顺序约束。

操作函数全部无锁（`prioq_insert`、`prioq_remove`、`prioq_head`），调用方自己管理互斥。

## 就绪队列的保护

```c
static PERCPU_DATA spin_t g_rdy_lock;  // 独立的自旋锁
static PERCPU_BSS prioq_t g_rdyq;      // 纯数据结构
PERCPU_BSS task_t *g_tid_next;         // 由 g_rdy_lock 保护
```

三个组件由同一把锁保护。不是把锁放在 prioq 里面，而是放在外面——这样 `task_pend` 可以拿锁、操作队列、更新 `g_tid_next`、释放锁，一气呵成。

## 抢占控制

```c
PERCPU_BSS int g_preempt_depth;   // >0 时中断返回不切换任务

void preempt_lock()   { THISCPU_ADD(g_preempt_depth, 1); }
void preempt_unlock() { THISCPU_ADD(g_preempt_depth, -1); }
```

只阻止任务切换，不阻止中断处理。中断仍然响应，`work_flush` 仍然执行，但 `int_return_to_task` 检查 `g_preempt_depth > 0` 时跳过 `g_tid_next` 的加载。

适用场景：批量 `task_start` + `notify_resched` 序列，或 `task_pend` 返回后到 `arch_task_switch` 之间的窗口。

与 `cpu_int_lock` 的区别：

| | preempt_lock | cpu_int_lock |
|---|---|---|
| 中断处理 | 照常 | 延迟 |
| 任务切换 | 阻止 | 阻止（副作用） |
| ISR 修改 g_tid_next | 可以（但不切） | 不能 |
| work_flush | 可以执行 | 不能执行 |

只在需要完全阻止中断时使用 `cpu_int_lock`（如 task_exit 中 work_defer 之后到 arch_task_switch 之间）。

`arch_task_switch` 在跳转到 `int_return_to_task` 之前清零 `g_preempt_depth`，确保显式切换后抢占恢复。

## irq_spin_take：自旋期间必须关中断

```c
int irq_spin_take(spin_t *spin) {
    int key = cpu_int_lock();
    uint32_t ticket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != ticket) {
        cpu_pause();   // 自旋期间中断保持关闭
    }
    lockdep_acquire(spin, &spin->dep);
    return key;
}
```

早期实现在自旋循环中调用了 `cpu_int_unlock(key)` / `cpu_int_lock()`，在 ticket-lock 模型下会导致死锁：

1. Task A 拿 ticket N，自旋时开中断
2. 定时器触发，ISR 返回路径中 `work_flush` → `logk` → `serial_puts` 拿同一把锁的 ticket N+1
3. ISR 上下文硬件关中断，自旋等待 ticket N+1
4. 锁释放后 service_counter = N，但 ticket N 的 Task A 被 ISR 阻塞
5. 死锁：Task A 不能运行以释放 ticket N，ISR 永远等不到 ticket N+1

结论：`irq_spin_take` 的语义就是"拿锁期间中断关闭"，必须全程保持。

## 调度器 sched_process

```c
// 由定时器中断调用，只负责同优先级轮转
void sched_process() {
    raw_spin_take(&g_rdy_lock);
    task_t *prev = THISCPU_GET(g_tid_next);
    task_t *next = containerof(prev->dl.next, task_t, dl);
    THISCPU_SET(g_tid_next, next);
    raw_spin_give(&g_rdy_lock);
}
```

使用 `raw_spin_take`（不关中断），因为调用者 `on_timer` 已在 ISR 上下文中。只轮转不切换——实际切换发生在中断返回的 `int_return_to_task`。

## CPU 选择策略

新任务挑选目标 CPU 的优先级：

1. **affinity 固定** → 使用指定 CPU
2. **当前 CPU 空闲** → 留在当前 CPU（缓存热）
3. **有其它空闲 CPU** → 选第一个空闲 CPU（`__builtin_ctzll(idle_mask)`）
4. **无空闲 CPU** → round-robin（`atomic_fetch_add(&g_next_cpu, 1) % cpu_count()`）

round-robin 零跨 CPU 访问、零锁竞争。配合已有的 `sched_try_migrate`（空闲 CPU 主动拉任务），负载均衡由创建时分配 + 定期拉取共同完成。

## 任务阻塞/恢复 API

调用方持有同步对象的锁（已关中断），分步骤操作：

```
task_pend(bits, wq, &pender, timeout, timeout_cb)
    ↓  从就绪队列移除，放入等待队列，注册超时 timer
    ↓  释放 g_rdy_lock，不切换任务
    ↓  调用方释放同步对象锁（开中断），抢占仍关闭
    ↓  调用方 arch_task_switch()（清零 preempt_depth）

—— 任务在此阻塞，CPU 切换到其他任务 ——

    ↓  被唤醒（正常唤醒或超时）
task_onresume(&pender)
    ↓  取消 timer，检查 expired 标志
    ↓  返回 PEND_WAKE 或 PEND_TIMEOUT
```

### task_pend

- 调用方持有 `wq` 所在同步对象的锁，中断已关闭
- 取 `g_rdy_lock`，从就绪队列移除自己，更新 `g_tid_next`
- 释放 `g_rdy_lock`（操作等待队列不需要此锁）
- 将 waiter 放入 `wq`，可选注册超时 timer
- 返回后调用方负责释放同步对象锁并 `arch_task_switch`

### prioq_wake_one

- 由同步对象释放资源时调用（如 `sema_signal`）
- 调用方持有 `wq` 锁
- 从 `wq` 摘除优先级最高的 waiter，调用 `task_cont` 恢复
- 如果目标 CPU 是远程，`task_cont` 内部发送 IPI

### task_onresume

- 任务恢复运行后调用
- 取消超时 timer（防止 timer 在 pender 析构后触发）
- 返回阻塞原因：PEND_WAKE（正常）或 PEND_TIMEOUT（超时）

## waiter_t 设计

```c
typedef struct waiter {
    dlnode_t   dl;       // 挂在 prioq 中
    prioq_t   *wq;       // 回指针，超时回调用来找锁
    task_t    *tid;      // 阻塞的任务
    ktimer_t   timer;    // 超时定时器，0 = 不超时
    int        expired;  // 超时标志
} waiter_t;
```

分配在调用者栈上（`sema_wait` / `msgq_recv` 等函数的局部变量）。任务被换出后栈保持有效，恢复后继续使用。

`wq` 回指针让超时回调能找到同步对象的锁。每个同步对象提供自己的超时回调，因为只有它知道锁的类型（`sema_t.lock`、`msgq_t.lock` 等）。

## task_exit

与 task_pend 不同，task_exit 是任务自行终止，不会返回：

1. TLB shootdown：通知其他 CPU 清除本任务栈的 TLB 条目
2. `irq_spin_take(&g_rdy_lock)`：关中断，从就绪队列移除
3. `work_defer(task_free)`：注册回收栈和 TCB 的 work
4. `irq_spin_give`：开中断
5. `arch_task_switch`：切换走，`work_flush` 在切换过程中执行 `task_free`

必须用 `irq_spin_take` 关中断，防止 `work_defer` 之后定时器触发 `work_flush` 而在切换前执行 `task_free`（会卸载当前正在运行的栈）。

## 锁顺序

```
阻塞路径：  wq->lock → g_rdy_lock → (arch_task_switch) → task_onresume
唤醒路径：  wq->lock → g_rdy_lock          (prioq_wake_one → task_cont)
超时回调：  wq->lock → g_rdy_lock          (timeout→task_wake_timeout→task_cont)
```

三条路径锁顺序一致，不会死锁。同 CPU 上超时回调不会在 `wq->lock` 持锁期间触发，因为调用方使用 `irq_spin_take` 关中断。

## IPI 通知

```c
uint64_t task_start(task_t *tid);   // 返回 cpumask
void notify_resched(uint64_t mask);  // 向远程 CPU 发 IPI
```

`task_start` 返回任务放置的目标 CPU 掩码，调用方收集后由 `notify_resched` 统一发 IPI。当前 CPU 自动从 mask 剥离——本地切换由 `arch_task_switch` 处理。

## 常见模式

### sema_wait

```c
int key = irq_spin_take(&s->lock);
if (s->count > 0) { s->count--; irq_spin_give(&s->lock, key); return 0; }

preempt_lock();
waiter_t w;
task_pend(TS_PENDING, &s->waitq, &w, timeout, sema_timeout);
irq_spin_give(&s->lock, key);
arch_task_switch();

int reason = task_onresume(&w);
return (reason == PEND_TIMEOUT) ? -ETIMEOUT : 0;
```

### sema_signal

```c
int key = irq_spin_take(&s->lock);
task_t *t = prioq_wake_one(&s->waitq);
if (!t) { s->count++; irq_spin_give(&s->lock, key); return; }
irq_spin_give(&s->lock, key);
notify_resched(1ULL << task_cont_cpu(t));
```

## 关键设计原则

1. **prioq 不带锁**：锁的粒度由使用者决定，覆盖一组相关的状态
2. **waiter 在栈上**：避免动态分配，task_t 保持紧凑
3. **超时回调由调用方提供**：同步对象自己管理锁，prioq 不关心锁的类型
4. **抢占控制与中断控制分离**：`preempt_lock` 只防切换不防中断，`cpu_int_lock` 全部阻止
5. **irq_spin_take 全程关中断**：避免 ticket-lock 反转死锁
6. **work_flush 在中断返回时总是调用**：即使抢占关闭。需要 protect 的 work（如 task_free）应在中断关闭时注册，确保在 `arch_task_switch` 的切换过程中安全执行
