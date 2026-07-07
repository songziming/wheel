# 如何设计 OS

操作系统耦合度很高，无法模块化。
模块化需要低耦合，也就是模块之间不需要知道太多信息，但 OS 做不到。
有些信息必然需要全局统筹，例如：
- 虚拟地址空间布局，划分哪些属于代码、数据，划分 percpu、中断栈等等
- 页面类型，
- 任务优先级，不同优先级有不同的调度策略

# 开发过程跟踪

- [x] 引导：使用 multiboot 1&2 引导，进入 64-bit mode，跳转到 higher-half
- [x] 输出：格式化字符串，打印调试输出
- 解析 multiboot 信息
  - [x] 解析物理内存布局
  - [x] 解析内核符号表
  - [x] 解析 framebuffer 信息并据此设置伪字符终端
- [x] 搜索并解析 ACPI 表
- [x] 解析 MADT，获取 Local APIC 和 IO APIC 信息
- [x] 根据 CPU 数量划分 PCPU 区域，使用 gs 快速访问当前 CPU 的变量
- [x] 分配页描述符数组，实现伙伴算法的物理页面管理
- [x] 记录内核虚拟地址布局
- [x] 开发 MMU 控制器，实现对映射方式的动态调整
- [x] 根据 CPU 数量划分中断栈，专用异常栈，保留 stack-guard
- [x] 将内核虚拟地址空间映射在页表中，切换到内核页表
- [x] 中断异常处理，记录中断深度，切换中断栈，切换 gsbase
- [x] 配置 Local APIC，使用 PIT 校准 Local APIC Timer，启用时钟中断
- [x] 开始执行任务，通过时钟中断实现任务切换
- [x] 创建任务时，自动分配栈空间，并记录在地址空间内
- [x] 启动多核，AP 快速进入 64-bit mode，并初始化
- [x] 实现多核抢占式调度，带优先级
- [x] 实现任务生命周期管理：创建、暂停、恢复、删除
- [x] 实现任务同步互斥：自旋锁、信号量、消息队列、管道
- [x] 实现 CPU 之间发送 IPI，实现参数传递（视硬件能力，自动发送 IPI 给负载最低的 CPU）
- [x] 实现 IO APIC，处理外部中断（PIT、HPET、键盘），转发给某个 CPU
- [x] 实现键盘中间件，8042 写入数据，shell 读取数据
- [x] 实现 ATA PIO 硬盘读写
- [x] 实现块设备驱动通用框架
- [x] 改进终端输出，klog 不再直接控制 framebuf，而是记录在 ringbuf
- [x] 优化 vmspace-unmap TLB-shootdown，向其他 CPU 发送 IPI 清除缓存

- [x] 更好的内存分配，不再限制大小为2的幂，不再要求物理内存连续
- [x] 优化 lockdep，无需侵入 TCB，信息保存在 percpu-var 里面
- [ ] 将 prioq、vmspace 改造成纯粹的数据结构，放在 lib，增加单元测试
- [ ] 优化 mmu 模块，unmap/remap 不需要考虑拆分大页的情况，使用者保证地址边界一致
- [ ] 命名优化：cpu_int_disable/cpu_int_restore/cpu_preempt_disable/cpu_preempt_restore
- [ ] 启用 SLUB 内存池，用来分配 TCB、fsnode、file_handle 这类对象
- [ ] 更好的对象管理，能遍历所有的 TCB，所有的 file_handle，file_handle 可以保存在 process_t 里面

- [ ] TCB、sema、mutex 这些对象都改成动态分配，可以强制删除，处于阻塞状态的task立即结束阻塞状态并返回错误码
- [ ] TCB 内部也包含一个event，任务结束的时候 signal_all，可以用 task_join 等待任务结束

- [ ] wdog_cancel 返回状态码，表示 wdog 是否成功取消，还是已经触发，避免执行无效的 task_cont
- [x] TCB 里面保存阻塞超时的 wdog，记录自己所在的waitq

# 远期优化点

内存管理相关
- [ ] 研究页面着色算法，记录各种颜色的直方图
- [ ] 分配物理内存、分配虚拟地址范围时，优先按 2M、1G 对齐分配，这样页表项的个数更少。
      可以让 arch 提供建议的对齐方案，再由 context 按照建议执行。
- [ ] 改进块设备缓存，改为组相联缓存。创建一个后台任务，负责更新缓存，与磁盘同步。
      还可以让 fs 缓存和 page-alloc 结合，凡是未分配的页面都能用来缓存 fs

并发与调度
- [x] 使用更好的自旋锁方案（MCS-lock、qspin），降低 cache-bouncing
- [ ] 实现读写锁，允许多个 reader 同时执行。调度器使用 read-lock，负载均衡使用 write-lock
- [ ] 识别处理器拓扑结构，改进调度算法

时钟相关
- [ ] 将 8264 PIT 制作成独立模块，用来校准 local apic timer；实现 HPET 支持，也用来校准 Local APIC timer
- [ ] 模仿 android VSYNC 校准机制，使用软件模拟锁相环。PIT 或 Local APIC timer 一直产生硬件中断，软件可以选择屏蔽或开启，防止产生累计误差
- [ ] 研究 TSC-deadline 模式，使用 tsc-adjust 调整每个 CPU 的 tsc 相位，让不同 CPU 的时钟中断错位开

代码风格
- [ ] 初始化阶段哪些函数需要在 BSP 运行，哪些需要在每个 CPU 运行，应该从函数名体现出来

调试
- [ ] 生成 trace，可以使用 perfetto 检查执行细节
