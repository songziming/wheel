#ifndef CPU_LOCAL_APIC_H
#define CPU_LOCAL_APIC_H

#include <def.h>

// 中断向量号，APIC 允许范围 16~255
// 向量号是 8-bit 整数，其中高 4-bit 是优先级，数字越大优先级越高
//

#define VEC_LOAPIC_TIMER    0xfc
#define VEC_LOAPIC_ERROR    0xfe
#define VEC_LOAPIC_SPURIOUS 0xff    // spurious 向量号最后 4bit 必须是 f

INIT_TEXT void local_apic_init_bsp();

#endif // CPU_LOCAL_APIC_H
