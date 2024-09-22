#include "loapic.h"

// APIC 的作用不仅是中断处理，还可以描述 CPU 拓扑（调度）、定时、多核
// 但是主要负责中断，因此放在中断模块下
// linux kernel 将 apic 作为一个独立子模块

// apic 的初始化却必须放在 smp 模块
