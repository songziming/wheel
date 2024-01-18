#ifndef CPU_LOCAL_APIC_H
#define CPU_LOCAL_APIC_H

#include <def.h>

// 中断向量号，APIC 允许范围 16~255
// 向量号是 8-bit 整数，其中高 4-bit 是优先级，数字越大优先级越高

// #define VEC_LOAPIC_TIMER    0xfc
// #define VEC_LOAPIC_ERROR    0xfe
// #define VEC_LOAPIC_SPURIOUS 0xff    // spurious 向量号最后 4bit 必须是 f

INIT_TEXT void local_apic_busywait(int us);

typedef enum local_apic_type {
    LOCAL_APIC_BSP,
    LOCAL_APIC_AP,
} local_apic_type_t;

INIT_TEXT void local_apic_init(local_apic_type_t type);

typedef enum local_apic_timer_mode {
    LOCAL_APIC_TIMER_ONESHOT,
    LOCAL_APIC_TIMER_PERIODIC,
} local_apic_timer_mode_t;

void local_apic_timer_set(int freq, local_apic_timer_mode_t mode);

INIT_TEXT void local_apic_send_init(int cpu);
INIT_TEXT void local_apic_send_sipi(int cpu, int vec);

void local_apic_send_eoi();
void local_apic_send_ipi(int cpu, int vec);

#endif // CPU_LOCAL_APIC_H
