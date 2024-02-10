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
- [x] 中断异常处理，记录中断深度，切换中断栈，切换 gsbase
- [x] 配置 Local APIC，使用 PIT 校准 Local APIC Timer，启用时钟中断
- [x] 开始执行任务，通过时钟中断实现任务切换
- [x] 创建任务，自动分配栈空间，并记录在地址空间内
- [x] 启动多核，AP 快速进入 64-bit mode，并初始化
- [x] 实现多核抢占式调度，带优先级
- [x] 实现任务生命周期管理：创建、暂停、恢复、删除
- [x] 实现任务同步互斥：自旋锁、信号量
- [x] 实现键盘中间件，8042 写入数据，shell 读取数据
- [ ] 实现块设备驱动

# 注意事项

## Debug and Release

Release 模式开启优化，要特别注意。

编译器优化模型是单线程，读写共享全局变量要加上 volatile 关键字。

## 中断触发模式

IO APIC 接受的外部中断怎样触发？edge/level？high/low？找出来源文档。
