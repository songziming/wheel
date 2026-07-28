# 阻塞/恢复/超时

> 本文分析任务阻塞/恢复/超时的实现细节，以及可能的并发情况

任务需要阻塞，等待某个内核对象状态变化，例如等待 semaphore-give。
其他任务执行 mutex_give、sema_give，需要将正在等待的阻塞线程唤醒。
如果 mutex、semaphore 被强制删除，也需要将正在等待的阻塞线程唤醒。
正在阻塞的线程，可以设定一个超时时间，等待超过这个时间就唤醒。

数据结构：
- 内核对象 semaphore，有一个自旋锁 sem.lock，包含一个阻塞队列 sem.waitq，受自旋锁保护
- 线程控制块 TCB，没有自旋锁，里面包含超时用的 tid.wdog
- TCB 还有几个辅助字段，受 sem.lock 保护（持有锁，且任务属于 sem.waitq 时才有效）：
    - tid.waitq     表示当前所在的阻塞队列，持有 sem.lock 时设置和清空
    - tid.waitlock  表示所在阻塞对象的自旋锁，消除 wdog 之后清除这个字段

用到的辅助库：wdog
- wdog_start 启动一个 wdog
- wdog_cancel 取消一个 wdog，如果 callback 已执行，则等待执行结束

### 阻塞自己

1. 获取 sem.lock
    1.1. （检查 sem 字段，判断无法得到信号量，需要阻塞等待）
    1.2. （原子地）修改当前任务状态为 pending
    1.3. 将当前任务从就绪队列取出（这一步需要获取就绪队列锁）
    1.4. 将当前任务放入 sem.waitq
    1.5. （可选）注册超时唤醒任务，如果超时则执行 on_task_timeout，记录阻塞对象的 lock
2. 调用 arch_task_switch 让出时间

### 唤醒一个任务

Task-A 唤醒 Task-B，以下是 Task-A 的执行步骤：
（此时 timeout-callback 可能正在执行，通过 sem.lock 和 wdog_cancel 保证安全）
1. 获取 sem.lock
    1.1. 取出 sem.waitq 的第一个元素 task_a
    1.2. 清除 task_a.waitq=NULL
    1.3. 标记 task_a.got=true，表示 task_a 是被正常唤醒的
2. （此时 task-A 只有我们持有 reference，不会有其他的访问——除了timeout——不持锁访问 Task-A 是安全的）
3. 执行 wdog_cancel，取消 task_a.wdog，如果此时 wdog 已经在执行，这一步会同步等待 timeout-func 结束
4. 清除 task_a.waitlock=NULL（此时 wdog 已经取消，timeout 不会执行，任务也尚未恢复运行，安全）
5. 修改 task_a 状态，去掉 pending-bit，发送 IPI

### 超时打断一个任务

Task-A 设定的超时计时器触发，执行 on_task_timeout 函数：
1. 根据 wdog 找到超时任务的 TCB，从 TCB 中读取阻塞对象的自旋锁 tid.waitlock
2. （只有 wdog_cancel 之后才能清除 tid.waitlock，这里访问一定是安全的）
3. 获取 tid.waitlock（这个锁就是 sem.lock，有可能得到锁的时候任务已经移出阻塞队列）
    3.1. 检查 TCB 是否已经移出 waitq（根据 tid.waitq 判定），若移出则退出
    3.2. 将 tid 移出阻塞队列
    3.3. 清除 tid.waitq=NULL、tid.waitlock=NULL
    3.4. 修改 tid 状态，去掉 pending-bit，发送 IPI

wdog 属于 TCB，所以访问 wdog 必然安全
wdog-cancel 之前，tid.waitlock 也是安全的（只有 wdog_cancel 之后才清除 waitlock）
至于 tid.waitq，它受到 waitlock 保护，得到锁就可以操作 tid.waitq

---

安全性分析

三个线程可能并发运行，可以从数据的访问控制入手分析。
保证每个字段同一时刻只能被一个线程使用。
当一个线程使用这个字段，需要确认其他线程此时不会执行，要么不会访问这个字段。

保证安全的手段：
- 自旋锁。例如 sem.lock，它保护 sem，以及 tid.waitq、tid.waitlock 两个字段
- unique_ptr，如果只有当前线程持有某个对象的指针，则对其访问一定安全。例如从 waitq 取出的任务，wdog_cancel 之后，只有一份引用
- 原子变量的自旋等待，例如 wdog_cancel 不断检查 wdog-state 的变化，这有些类似自旋锁