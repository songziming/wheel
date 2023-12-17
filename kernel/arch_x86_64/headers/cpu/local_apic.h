#ifndef CPU_LOCAL_APIC_H
#define CPU_LOCAL_APIC_H

#include <def.h>

// 中断向量号
// APIC 允许的中断向量号为 16~255
#define VEC_LOAPIC_SPURIOUS 0x4f
#define VEC_LOAPIC_TIMER    0xfc

INIT_TEXT void local_apic_init_bsp();

#endif // CPU_LOCAL_APIC_H
