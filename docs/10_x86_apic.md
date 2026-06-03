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

## 发送 IPI 附带参数

IPI 可能需要附带参数，例如通知其他 CPU 恢复运行一个 task，需要传递 TID。
通知另一个 CPU 停止当前正在运行的 任务，也需要传递目标 TID。
Local APIC 应该没有这个功能，只能通过共享变量传参。

## 配置 APIC Timer

时钟有 singleshot 和 periodic 两种模式。
timer 内部有一个计数器寄存器，按固定速度递减。每次减少到0发送一次中断。

每个 CPU 单独配置，彼此没有关联？
可以校准每个 CPU 的频率和相位
由统一的时钟（PIT、HPET）向所有 CPU 广播时钟中断，频率是 `sys_clk_freq*ncpu`。
每个 CPU 接受前面几个中断，用来计算频率（大小核 CPU 会不会频率不同？故每个 CPU 单独计算）
然后再收到中断时，写入 apic-timer ICR，开始计时。
每个 CPU 算好自己的相位，在自己的时间点开始计时。
所有 CPU 都校准完成，由最后一个 CPU 负责关闭外部时钟中断，结束这次校准。
可以专门划出一个 IRQ 用来校准时钟。


## TSC-deadline mode

这是 Local APIC Timer 的一种工作模式。
这种模式下，CPU 的时间戳寄存器（TSC）就是时钟计数器。
当时间戳达到或超过 IA32_TSC_DEADLINE MSR 的取值，就产生一个中断。

需要检查 TSC 速度会不会变，TSC 即使停在 hlt 指令也会增长。

CPUID.80000007H:EDX[8] 可以判断 Invariant-TSC，表示 TSC 频率固定不变。
表示 CPU 处于任何电源状态下（ACPI-P/C/T），TSC 速度都不变。

类似于 singleshot，只产生一次中断。
需要再次写 IA32_TSC_DEADLINE MSR，重新一个计时周起。

## 如何同步各个 CPU 的 TSC

TSC 可读可写，但 cpu 只能修改自己的 tsc，不容易将所有 CPU 的 tsc 同步。

有两个寄存器：
- IA32_TIME_STAMP_COUNTER，这个是 tsc 本身，可以用 wrtsc 修改
- IA32_TSC_ADJUST，这个表示相位
这两个寄存器是联动的，更新其中一个，另一个自动更新。只是同步的不是值，而是变化量。

tsc 随时间变化，但 tsc_adjust 不随时间变化，二者关系如下：
tsc(t) = tsc_adjust + t
所以，调整 tsc_adjust，就可以修改各个 CPU 的 tsc 相位
