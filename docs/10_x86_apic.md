# APIC

IO APIC 接收硬件产生的中断，将它们转发给 Local APIC。
Local APIC 处理 CPU 自己的中断异常、其他 CPU 发来的 IPI，还能产生时钟中断。

我们需要 IO APIC 实现的功能：
- 可以将中断转发给任意 CPU
我们需要 Local APIC 实现的功能：
- 能够发送IPI，实现单播和广播
- 以固定周期发送时钟中断，作为系统调度信号

## IO APIC 发送中断给任意 CPU

IO APIC 重定位条目类似于路由表，描述了每个中断发送给哪个 CPU。
然而 x2APIC 不包括 IO APIC，重定位条目中，APIC-ID 只有 8-bit。
如果处理器的 x2APIC-ID 超过了 8-bit，重定位条目容不下，该怎么做？
interrupt-remapper？

## 发送 IPI

我们只需要单播和广播，不需要多播（向多个 CPU 发送 IPI）。
多播可以通过循环实现，我们不要求 IPI 在同一时刻发出去。

physical/logical mode
physical 模式下，向 ICR 填入目标 CPU 的 apic-id，就可以向某个 CPU 发送 IPI。
logical 模式下，OS 可以给每个 CPU 指定 cluster-id 和 logical-id，可以向一个 cluster 发送 IPI，实现类似多播的效果。
我们使用默认的 physical mode 即可。

实现广播，可以借助 ICR.dest_shorthand，优先级在 destination 字段之前：
- no shorthand，使用 dest 确定目标
- self，发送 IPI 给自己（x2APIC 还有另一种 self IPI 方式）
- all including self
- all excluding self

## 配置时钟

时钟有 singleshot 和 periodic 两种模式。

timer 内部有一个计数器寄存器，按固定速度递减。每次减少到0发送一次中断。
