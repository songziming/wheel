# APIC driver

local APIC 用来处理中断，每个 CPU 都有自己的 local APIC。

local APIC 的作用：
- 处理 CPU 自己产生的中断、异常
- 响应其他 CPU 发送来的中间（IPI）
- 时钟

发送 IPI 需要指定目标 cpu 的 ID（IPI 还有多播、广播的用法）
写入 ICR 寄存器（interrupt command register），这个寄存器中，dest 字段只有 8-bit
也就是说，目标 CPU-ID 必须能用 8-bit 装下

ICR[dest shorthand] 有 4 种选项，可以不用 dest 快速指定 IPI 目标：
- no shorthand，使用 dest 确定目标
- self，发送 IPI 给自己（x2APIC 还有另一种 self IPI 方式）
- all including self
- all excluding self


