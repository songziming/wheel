# 多任务任务

task 是最基本的调度单位。可以创建、暂停、恢复、删除。

## 任务的状态

- 就绪态，可以运行，放在就绪队列中
- 非就绪，因为某种原因不能运行，原因可能有（可以组合）：
  - 阻塞，正在等待信号量、消息队列等资源
  - 暂停，没有等待任何资源，被其他任务修改，例如正在被调试、任务刚创建

就绪态的任务需要放在 ready-queue 里面，任务状态切换时，只有就绪/非就绪的切换重要。
ready-queue 是 percpu-var，每个 CPU 只处理自己的 ready-queue。
如果我们要把一个 task 放在其他 CPU 上运行，则发送 IPI。

## 停止任务（ready->nonready）

获取 mutex/semaphore 阻塞，或者读文件等待异步磁盘操作。
task_pend 只能由自己执行，可以认为需要停止的就是当前任务。
task_stop 可以强行停止另一个任务，可以用来强行结束一个卡死的任务。
（kill-task 可以在中断里执行，向目标 CPU 发送一个 IPI 触发中断，也可以等待 tick）

假设 task_pend 停止的必然是当前任务，当前任务必然位于当前 CPU 的 ready-queue，可以使用 thiscpu。
从 thiscpu(ready_q) 移除 thiscpu(tid_prev)，任务就完成了。
接下来执行 sched_process，选出一个新的任务设为 tid_next。
然后调用 arch_task_switch，模仿中断，切换到新选出的任务。

如果任务需要删除，可以注册一个 work，在中断返回阶段执行。
中断进入时，需要访问 TCB 保存上下文。中断退出时，待删除 TCB 不再使用，可以安全释放。

## ISR 里面检查任务停止

停止另一个 CPU 的任务需要发送 IPI，但目标 CPU 可能其他中断先到来。
所有 ISR 都应检查 tid_prev 是否结束，如果结束则移除 ready-queue。
这样，IPI-resched 不需执行任何操作，触发一次中断就足够了。

## 恢复任务（nonready->ready）

目标任务当前没有运行，只要挑选一个 CPU 即可。
跨 CPU 操作，使用 percpu_ptr 访问目标 ready-q？
或者发送 IPI，直接让目标 CPU 执行 task_resume。
新的任务切换到 ready，有可能抢占，有可能触发任务切换，有可能需要中断。
既然需要中断，还不如无条件发送 IPI，这样 ready-q 总是由当前 CPU 处理，代码更简单。

---

### 阻塞一个任务

许多地方需要阻塞/恢复任务，例如等待获取mutex、等待kobj释放、等待task结束。
使用阻塞队列/等待队列记录所有阻塞状态的任务。
每个阻塞任务还可以设定超时时间，如果超时就不再等待，将阻塞线程提前唤醒。
如果等待条件得到满足，则唤醒一个或全部阻塞线程。
还有一种恢复任务的情况，那就是等待的对象需要销毁，例如删除semaphore/mutex，所有等待的线程都要结束阻塞，返回错误码

这里涉及到三个线程、三个函数：
- 被阻塞线程 A，调用 task_pend 将自己放入 pend-queue，设定一个超时 wdog
- 超时线程 T，在 CPU0 wdog ISR 里面调用 task_wake_timeout，将 A 移出阻塞队列并唤醒
- 唤醒者线程 B，检测到条件满足，调用 task_unpend_one 将 A 移出阻塞队列并唤醒，其中包括调用 wdog_cancel，取消超时 wdog

这三个线程的三个函数需要防止竞争，需要锁住 wait-queue。
删除定时器的时候需要等待 timeout callback 结束，两个线程

obj 可能被删除，唤醒后访问可能不安全，需要上锁。
wdog 定义在阻塞线程的栈上，必然安全

~~~
// 被阻塞线程 A，等待 obj 释放
void task_A(kobj_t *obj) {
  pender_t pender;

  {
    lock_guard guard(obj->lock);
    wdog_start(pender.wd, timeout, pend_timeout);
    pender.tid = this_task;
    pender.user = obj;
    obj->waitq.enqueue(pender);
  }
  arch_task_switch();

  // 已恢复，检查 pender.state，判断恢复的原因（对象已删除？超时？）
  if (pender.is_timeout) {
    // 如果是超时，obj 可能未删除，也可能已经删除，都不确定
    // pender 还在队列里面
    // 后面 obj 真的释放时，本线程会被再次唤醒一次
    // 如果本线程后面又休眠，恰好赶上 obj 真的释放，本线程会被错误唤醒
  }
}


// 超时线程 T，在 isr 里面执行
// 本函数执行的时候，对象可能已经删除
// 访问 wdog 一定是安全的，wdog->tid 也是安全的
// 可以通过 wdog 字段返回唤醒状态
void pend_timeout(wdog_t *wd) {
  pender_t *pender = containerof(wd, pender_t, wd);
  kobj_t *obj = pender->user;

  pender->is_timeout = true;
  task_cont(pender->tid);
  // pender 还留在 obj.waitq 里面（obj 可能已经没了）

  <!-- {
    lock_guard guard(obj->lock);
    // wdog 已经触发，不用 cancel
    pender->expired = true;
    obj->waitq.dequeue(pender);
    task_cont(pender->tid);
  } -->
}


// 唤醒者线程 B，释放对象 obj，并且唤醒 A
void task_B(kobj_t *obj) {
  {
    lock_guard guard(obj->lock);
    if (0 == --obj->refcnt) {
      foreach (pender_t *pender : obj->waitq) {
        wdog_cancel(pender->wd); // 这需要等 wdog-callback 执行结束（忙等待）
        pender->expired = false;
        obj->waitq.dequeue(pender);
        task_cont(pender->tid);
      }
    }
  }
}
~~~

---

### 现在的阻塞/恢复/超时机制

#### 数据结构：waiter 合入 TCB

`waiter_t` 已删除，阻塞相关字段直接放进 `task_t`：

```c
typedef struct task {
    ...
    dlnode_t    dl;          // 复用：在 readyq 中（就绪/运行）或在 waitq 中（阻塞）
    _Atomic uint32_t state;  // TS_READY/TS_STOPPED/TS_PENDING/TS_DELETED 位掩码
    ...
    wdog_t      timer;       // 超时定时器，触发后调用 task_timeout
    prioq_t    *wait_wq;     // 阻塞在哪个 waitq（给 timeout callback 用）
    spinlock_t *wait_lock;   // 该 waitq 的锁（给 timeout callback 用）
    int         got;         // 是否被正常唤醒（非超时）
    int         expired;     // 是否因超时被唤醒
} task_t;
```

要点：

- `dl` 同一时刻只在一个队列里：就绪/运行时在 readyq，阻塞时在 waitq，暂停/将退出时都不在。`TS_PENDING` 的语义就是"dl 位于某个 waitq 中"。
- `timer`/`wait_wq`/`wait_lock`/`got`/`expired` 仅当 `state` 含 `TS_PENDING` 时有效，由该 waitq 所属对象的锁保护。
- wdog 在 TCB 里（不在栈上），TCB 在 task_free 之前一直有效，而阻塞着的任务不可能跑 task_free——所以 timeout callback 访问 `&tid->timer` 永远有效，没有栈生命周期问题。

#### 三个参与者、三个原语

一次阻塞-唤醒涉及三方：

- **被阻塞线程 A**：调 `task_pend(wq, lock, timeout)` 把自己放入 waitq 并 arm wdog。
- **唤醒者 B**：条件满足时调 `task_unpend_one(wq, lock)`（或 `task_unpend_claim_nolock` + `task_unpend_finish`）把 A 摘出 waitq、cancel wdog、唤醒。
- **超时回调 T**：在 CPU0 的 wdog ISR 里，由泛型 `task_timeout` 调 `task_wake_timeout(tid)` 把 A 摘出 waitq 并唤醒。

关键拆分：`task_unpend_one` = `task_unpend_claim_nolock`（锁内摘头、置 got）+ `task_unpend_finish`（锁外 wdog_cancel + task_cont + IPI）。`task_wake_timeout` 在回调里、持 `wait_lock` 执行。

#### 锁的职责划分

- **obj->lock（waitq 所属对象的锁）**：保护 waitq 链表结构 + TCB 的 wait 字段（got/expired/wait_wq/wait_lock）+ "谁是唯一唤醒者"的 claim。sema/mutex/msgq/kobj 各自的 lock 就是这把。
- **g_rdy_lock（percpu）**：保护 readyq 成员关系。
- **wdog CAS 状态机 + timer_lock**：保护定时器队列与 callback 的执行权。

obj->lock **不**保护 TCB 的生命周期，只保护 waitq 相关字段；TCB 生命周期由"线程是否阻塞"保证。

#### 需要考虑的竞争与规避

**竞争 1：B 与 T 同时唤醒同一个 A（双重唤醒）**

B 和 T 都可能拿到 A 的 TCB（B 通过 waitq 头，T 通过 wdog 反查）。若两者都 `task_cont`，A 被插入 readyq 两次、state 被清两次（断言失败/数据结构损坏）。

规避：**waitq 摘除是锁内唯一的 wake claim**。两条路径都必须先在 obj->lock 下从 waitq 摘掉 A 的 dl，才能 `task_cont`：

- B 路径：`task_unpend_claim_nolock` 在锁内摘除。
- T 路径：`task_wake_timeout` 在锁内用 `prioq_contains` 复核"还在不在 waitq"，在才摘除并唤醒；不在就早返回。

因为摘除在锁内互斥，只有一个赢家。wdog"找到 TCB"不等于"唤醒"——`prioq_contains` 复核是这道闸门，不能省。

**竞争 2：wdog_cancel 持对象锁 → 与 timeout callback 死锁**

若 B 在持 obj->lock 的状态下调 `wdog_cancel`，而 T 的 callback 又要获取 obj->lock 才能安全操作 waitq，则：B 持锁等 callback 结束，callback 等 B 的锁，循环等待。系统级后果还会把 timer_lock 也一起卡死（wdog_cancel 自旋期间持有 timer_lock），CPU0 的 wdog_process 停摆。

规避：**claim/finish 拆分，wdog_cancel 与 task_cont 都在 obj->lock 之外执行**。`task_unpend_claim_nolock` 在锁内只做"摘除 + 置 got"，出锁后 `task_unpend_finish` 才 `wdog_cancel` + `task_cont`。这样 callback 能拿到锁跑完，wdog_cancel 的自旋立刻结束，无环。

需要"锁内原子判断空并做别的操作"的场景（如 `sema_give` 的"空则 value++、非空则唤醒一个"、`mutex_give` 的"交接 owner"）用 `task_unpend_claim_nolock` 在锁内 claim，出锁后 `task_unpend_finish`。

**竞争 3：waiter/wdog 的 use-after-free**

若 A 被 `task_cont` 唤醒后立刻在另一 CPU 跑起来、从 `sema_take` 返回并销毁局部量，而 B 还在访问它 → UAF。

规避：wdog 不再在栈上，而是 TCB 字段，TCB 在 task_free 前一直有效。同时 `task_unpend_finish` 严格 **先 `wdog_cancel` 再 `task_cont`**——A 在 cancel 完成前不会跑起来，wait 字段一定有效。

**竞争 4：kobj 被 free 后 timeout callback 访问它（UAF）**

timeout callback 通过 `tid->wait_wq`/`wait_lock` 找到 obj 并取锁。若 obj 已被 `kobj_release_nolock` 释放，回调拿到野指针。

规避：**obj 在所有 wdog cancel 之前不释放**。`kobj_release_nolock` 的唤醒循环 `while (task_unpend_one(&obj->waitq, &obj->lock))` 每次迭代都会 `wdog_cancel`（在 `task_unpend_finish` 里），而 `wdog_cancel` 会等正在跑的 callback 结束才返回。所以循环结束时没有任何 callback 还会触发，obj 才被 `pool_free`。callback 访问 obj 时 obj 必然还活着。

**竞争 5：stale wdog_cancel 误清新的 re-arm**

A 被唤醒后可能再次阻塞（msgq 的循环重试），重新 `wdog_start(&tid->timer)`。若旧的 `wdog_cancel` 还没结束就 re-arm，会误清新 arm 的 wdog。

规避：**ordering 保证**。`task_unpend_finish` 里 `wdog_cancel` 在 `task_cont` 之前完成，A 要等 `task_cont` 才能跑起来，所以 A 跑起来、可能再次 pend 时，旧 cancel 已结束。加之 wdog 状态机要求 `WDOG_IDLE` 才能 arm（被 cancel 置 IDLE、被 fire 跑完也置 IDLE），re-arm 时 wdog 必然是 IDLE。

**竞争 6：state 字段的并发读写**

`task_t->state` 是位掩码，被 A 自己（task_pend 置 TS_PENDING、task_exit 置 TS_STOPPED）和唤醒路径（task_cont 清位）读写。

规避：`state` 改 `_Atomic`，置位用 `atomic_fetch_or`、清位用 `atomic_fetch_and`、整体赋值用 `atomic_store`、读取用 `atomic_load`。由于竞争 1 已保证"每次唤醒只有一方 task_cont"，清位不会并发冲突；原子操作把这条不变量从隐式契约变成显式语义。

#### wdog 状态机与 cancel 保证

```
WDOG_IDLE ──[wdog_start]──→ WDOG_ARMED ──[wdog_process]──→ WDOG_FIRED ──[callback done]──→ WDOG_IDLE
    ↑                          │                                │
    └─────[wdog_cancel]────────┘                                │
    ↑                                                           │
    └──────────────[wdog_cancel: spin-wait]─────────────────────┘
```

核心保证：**`wdog_cancel` 返回 ⇒ callback 不会（再）被调用**。这是 kobj/sema 等动态对象能安全释放的前提。实现上：

- 若 wdog 仍 `WDOG_ARMED`（在队列）：CAS 置 `WDOG_IDLE` 并从队列摘除，callback 永不执行。
- 若 wdog 已被 `wdog_process` 摘下（不在队列）：CAS 失败，说明 callback 正在执行（`WDOG_FIRED`）或已结束（`WDOG_IDLE`）。此时无需操作链表，**放掉 timer_lock 再自旋**等 `WDOG_FIRED→WDOG_IDLE`，避免持 timer_lock 自旋卡死 CPU0 的 `wdog_process`。

`wdog_process` 在 CPU0 的 local-APIC timer ISR 里推进全局队列；其余 CPU 的 timer ISR 只跑 `sched_process`。

#### 跨 CPU 唤醒

`task_cont` 选定目标 CPU 后，把 A 插入目标 CPU 的 readyq，若跨 CPU 则发 `VEC_IPI_RESCHED` IPI。IPI handler 只需 EOI——真正的切换发生在目标 CPU 中断返回路径的 `iret_to_task` 读 `g_tid_next`。所以"readyq 总由目标 CPU 自己处理"，唤醒方只负责入队 + 发 IPI。


