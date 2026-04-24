# 自旋锁

最简单的互斥机制，防止内核不同线程之间数据竞争。

导致竞争的原因有三种：
- 物理多核导致竞争，自旋锁可以保护
- 中断/异常导致竞争，禁用中断可以保护（异常无法禁用）
- 任务抢占导致竞争，禁用抢占可以保护

自旋锁有多种实现方案，各有优缺点。
我们可以提供多种实现，暴露相同的 API，通过配置选择使用的方案。

评价自旋锁好坏的指标有哪些？
- 性能
- 公平性

## atomic-test-and-write

利用 CPU 原子指令，读写一个共享变量

~~~
spin_lock:
.retry:
    lock bts [lock], 0
    jc      .retry
    ret

spin_unlock:
    lock btr [lock], 0
    ret
~~~

bts 表示 bit test-and-set，btr 表示 bit test-and-reset。
将目标内存的某个 bit 放入 eflags.CF，bts 将目标 bit 写入 1，btr 写入 0。

目标变量 lock，取值 0 表示未被持有，取值 1 表示被持有。
将 lock 设为 1，且之前的状态是 0，说明我们成功获取到锁。
如果将 lock 设为 1，但之前的取值也是 1，说明其他线程正持有该锁，需要自旋。

缺点：lock 前缀锁住的是内存总线，其他 CPU 访问其他地址也会受限。
自旋的时候频繁 lock，会显著干扰其他 CPU。应该减少 lock 用量。

## atomic-test-and-write 2

~~~
.spin_wait:
    pause
    test    [lock], 1
    jnz     .spin_wait  # still locked, keep waiting
spin_lock:
    lock bts [lock], 0
    jc      .spin_wait
    ret
~~~

自旋过程使用 test，这个读指令是原子性的。一旦发现 lock free，重新执行 lock bts。
当前，有可能误报，test 发现 lock free 的时候，可能锁被另一个 CPU 抢到。

自旋过程中的 pause 是 Intel 建议的，防止超线程相互干扰。

## 缓存优化

为了防止 false-sharing，自旋锁应该完整占据一个 cache-line。
包含自旋锁的 cache-line 必然被共享，只能尽量不影响其他数据。

## ticket-spinlock

前面的自旋锁无法保证公平性，ticket spinlock 能确保先到先得，VxWorks 用的就是这种。

但是 ticket spinlock 仍存在 cache-line bouncing 问题。也就是多个线程仍需访问同一段 cache-line。

## MCS lock

全程 Mellor-Crummey & Scott lock。

每个等待锁的申请者都有自己的 lock_waiter 节点，以链表形式追加到 spinlock 之后。
链表的顺序就是尝试获取锁的顺序，因此 MCS-lock 也能保证先到先得。
每个申请者只需在自己的节点上自旋，访问的 cache-line 不会和其他线程冲突。
释放自旋锁，就是从链表中移除当前元素，然后通知后面的节点。

mcs_lock_t 记录链表的尾节点，这样新的线程无需遍历就能找到当前链表的末尾。

MCS-lock 也有缺点，锁节点占用空间太大。page desc 这类数据结构对空间很敏感。

## K42 lock

对 MCS-lock 的简单改进。
MCS-lock 的问题在于更改了 API，代码仓库中无法直接替换，所有用到自旋锁的地方都要修改。
K42 lock 则不需要传入 lock_waiter 节点，而是直接在栈上创建。

## qspinlock

qspinlock 可以将 MCS-lock 的功能放进 32-bit 字段。

MSC-lock 占空间大，因为需要保存一个指针。
然而，申请者与 CPU 严格对应，可以使用 cpu-id 替代指针。
每个申请者持有自己的 qspin-waiter，可以声明为 percpu-var。

使用 16-bit 保存链表中最后一个申请者的 cpu-id，再使用一个 bit 表示锁状态。
如果进一步压缩，甚至能压缩到 16-bit。

持有锁的状态下，waiter 也要保留在队列中。
如果此时有另一个申请者尝试获取锁，它必须知道应该把自己的 waiter 追加到哪个节点之后。
释放锁的时候，检查自己的后继，通知后继申请者。

同一个线程，如果申请两个 qspinlock，会怎样？
持有 lock1，需要 waiter1 保留在队列中，尝试获取 lock2，又需要 waiter2。
Linux 的做法是，不允许同一个 context 获取多个 qspin，每个 CPU 最多四种 context：
- task
- softirq
- hardirq
- nmi
所以使用 PERCPU 定义四个 struct qnode，作为 waiter 节点
