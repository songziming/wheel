# 用户模式

内核模式/用户模式是同一个任务之下的两种状态。
sysret 从内核态切换到用户态，syscall 从用户态切换到内核态。

进入用户态，需要准备的资源：
- 设置 tss-rsp0，每次切换任务都设置 tss
- 各种入口点（中断异常）添加来自 ring3 的检测，swapgs

作为一个任务，主动切换到 ring3，准备资源：
- 分配代码段数据段（可以合并），拷贝数据
- 分配用户栈
- 跳转到用户态开始执行

### 系统调用

使用快速指令 syscall/sysret
这个指令不会操作栈，调用之后仍处于用户栈。

### GDT 不能只读

第一次访问一个 section descriptor，CPU 会将 access 设为 1。
这会产生一个 memory write operation。
如果GDT所在的虚拟地址恰好不可写，就会产生 page fault。
