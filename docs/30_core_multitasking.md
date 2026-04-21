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

