# APIC driver

local APIC 用来处理中断，每个 CPU 都有自己的 local APIC。

local APIC 的作用：
- 处理 CPU 自己产生的中断、异常
- 响应其他 CPU 发送来的中间（IPI）
- 时钟

### 我们的目标

1. 可以向任意 CPU 发送 IPI，可以向任意的 CPU 组合发送多播 IPI
类似于 cpuset 位图
发送 IPI 的时候，可以选择 physical/logical 两种模式
如果向一个 CPU 发送 IPI，使用 physical mode，指定目标 apic-id 即可
如果要向多个 CPU 发送 IPI，可以使用 logical mode，每个 CPU 占据 LDR 一个比特（最多支持 8 个 CPU）

2. IO APIC 可以将中断转发给任意 CPU

x2APIC 只更新了 local apic，没有更新 IO APIC。
IO APIC 的重定位条目里，只能装下 8-bit APIC-ID，x2APIC 容不下。
因此需要 interrupt-remapper。
TODO 具体怎样 remap？

### 关于 APIC-ID

apic-id 可以用作 IPI destination。
xAPIC 使用 8-bit 表示（早期的 P6 只有 4-bit），IPI.dest 也是 8-bit
x2APIC 使用 32-bit 表示 apic-ID，IPI.dest 也是 32-bit

### 发送 IPI 确定目标

发送 IPI 需要指定目标 cpu 的 ID（IPI 还有多播、广播的用法）
写入 ICR 寄存器（interrupt command register），这个寄存器中，dest 字段只有 8-bit
也就是说，目标 CPU-ID 必须能用 8-bit 装下

ICR[dest shorthand] 有 4 种选项，可以不用 dest 快速指定 IPI 目标：
- no shorthand，使用 dest 确定目标
- self，发送 IPI 给自己（x2APIC 还有另一种 self IPI 方式）
- all including self
- all excluding self


如果 ICR.dest_mode == physical, ICR.dest 字段直接表示目标 cpu 的 apic-id
xAPIC 模式下，ICR.dest 为 8-bit
x2APIC 模式下，ICR.dest 为 32-bit，始终与 APIC-ID 相匹配

如果 ICR.dest_mode == logical，ICR.dest 表示 MDA（message destination address）
LDR 寄存器可以用来设置 logical-apic-id，相当于 OS 可以自己重新设置目标 ID
如何比较 MDA 和 LDR，并不是判断两个 uint8 是否相等
- DFR == flat_model（默认），使用按位与，if (0 != (MDA & LDR)) then send IPI
- DFR == flat_cluster_model（该模式只用于 P6 和奔腾，我们不用考虑）
- DFR == hierarchical_cluster_model，将cpu分组，每组最多4个cpu。