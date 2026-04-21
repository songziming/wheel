# 多核任务调度算法

调度算法只管理 ready 状态的任务。
一个好的调度策略需要满足以下目标：

- CPU 利用率高，只要还有 ready task，CPU 就不能休息
- 抢占式调度，避免阻塞，先来的任务不能独占CPU导致后面的任务得不到运行
- 按优先级调度
- 较少迁移，算法尽量在一个CPU之内完成
- 负载均衡（对 cpu 而言公平）
- 公平性（对 task 而言），分配到不同 CPU 的任务需要按差不多一样的速度运行

### 我们并不需要那么完美的调度器
现实情况是，大部分进程处于 pending，并不会长时间运行。
更需要的是响应速度，当 mutex 获取成功时立即恢复。
公平性反而不重要，因为我们一般感受不到大量 ready task 相互轮转的情况。
只要让能运行的 task 运行起来就行——“应跑尽跑”。

### 调度算法&任务状态切换
调度只关心 ready task。如果任务状态变化，可能需要移出 ready-queue，或放入 ready-queue。
通常是 task-manager 调用 scheduler。
首先获取 task.mutex，然后获取 readyq.mutex。

### Linux CFS 设计思路
重点是“绝对公平”，使用 vruntime 记录每个进程已运行的时间。
所有进程按 vruntime 排序，记录在 rbtree 里面，根节点是 vruntime 最小的，也就是执行时间最落后的。
每个 CPU 都有自己的 runqueue，定期执行 rebalance。
（既然只需要找出 vruntime 最小的进程，不需要整个队列有序，使用最小堆是否更好？）

### 时钟中断
按固定间隔发送时钟中断，中断里更新 task 执行事件，检查任务轮转。
但某些特殊情况下，时钟中断可以屏蔽：
- IDLE 状态，没有其他 ready task，无需接收 timer interrupt
- 只有一个 ready task，不需要轮转，无需接收 timer interrupt
不过，时钟中断还有别的作用，timer 可以执行定时任务，在若干 tick 之后执行某个函数。
但这种 timer 只在 cpu0 运行。

### tickless
还有一种更激进的时钟中断策略，完全不使用 tick。
RTOS 适合这种，因为完全按优先级调度，无需轮转。
tickless 不代表完全没有时钟中断，只是间隔不固定。
scheduler 动态计算下一个任务的时间片，动态设置一个 deadline

tickless 并不是没有时间片，而是直接按分配的时间片设定下次时钟中断的时间。
tick-based-scheduler 只能按 tick 为基本单位分配时间片，粒度大。tickless 则实现了无级调节。
非常适合 APIC timer tsc-deadline mode，按指令周期计算时间片。

### 全局队列？本地队列？

ready-queue 用来记录 ready tasks，有 global/percpu 两种方案。

全局就绪队列：
- 每个 CPU 都向这个数据结构申请 task，需要加锁，或者实现为 lockless，比较复杂
- 取到的任务必须从 ready-queue 里面移除，时间片结束再放回，中断里要做的事情更多
- 公平性更好，取到的一定是当前最需要运行的任务
- 缓存不友好，任务会在不同 cpu 之间迁移

使用 CPU 本地就绪队列：
- 每个 CPU 只检查自己的 queue，实现代码简单直接
- 当前运行的 task 可以一直留在队列中，不用一直操作队列
- 缓存友好，迁移较少
- 公平性差，需要不定时执行 rebalance，将某些任务迁移到其他 CPU
