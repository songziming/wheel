# 线程迁移实现同步的 RPC

讨论帖：https://forum.osdev.org/viewtopic.php?t=58087

RPC 由一个进程发起，另一个进程执行，相当于同一个 task 改变了所属的进程。

发起 RPC，通过 syscall 陷入内核态，内核修改 TCB.pgtable 切换到服务进程的地址空间，回到用户态。
还是同一个 task，一直处于 ready 状态，不涉及阻塞恢复。
优先级不变，高优先级的 client 调用一个低优先级的 server 不会被中优先级抢占，没有优先级反转问题。

缺点：
需要分配大量栈，每打开一个 endpoint 都需要在目标地址空间下创建一个 stack。
如果有 M 个server，N 个 client，那么最多需要 M*N 个栈
如果想要限制 rpc stack 的数量，就需要限制 server 同时处理的 rpc 数量

或者将 server-stack 改造成资源池，只有 client 数量增多时再创建新的 stack
这样即使 N 个 client 都和 server 交互，只要它们不是同时交互，就可以少用几个栈。
需要避免rpc内部阻塞，这可能导致任务休眠，让更多的 client 发起调用

## 谁来提供栈

两个进程分别称作 client、server。迁移过程，task 需要一个内核栈，两个用户栈。
调用之前，task 属于 client，拥有的用户栈属于 client。
调用过程中，task 临时属于 server，使用 server 空间里的用户栈。

这个 server stack 由谁来分配？

server 分配，那么只允许有限数量的调用者，更多进程发起 RPC 只能等待。
使用一个 semaphore 保护 stack pool，取出一个server-stack 就可以迁移 vmspace

client 分配，需要调用者打开一个 endpoint，里面记录着 server 进程的 kobj，分配一个 server 地址空间里的用户栈。
client 进程里，同一时刻只能有一个线程使用这个 endpoint。
创建 endpoint 时，指定一个 service-name，内核根据名称找到 server process，分配一个用户栈。
client 进程退出的时候，将这个 endpoint 销毁，释放对 server-proc 的引用。

