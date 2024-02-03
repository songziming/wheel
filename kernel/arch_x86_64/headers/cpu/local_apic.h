#ifndef CPU_LOCAL_APIC_H
#define CPU_LOCAL_APIC_H

#include <def.h>


// typedef enum local_apic_type {
//     LOCAL_APIC_BSP,
//     LOCAL_APIC_AP,
// } local_apic_type_t;

INIT_TEXT void local_apic_init();

typedef enum local_apic_timer_mode {
    LOCAL_APIC_TIMER_ONESHOT,
    LOCAL_APIC_TIMER_PERIODIC,
} local_apic_timer_mode_t;

void local_apic_timer_set(int freq, local_apic_timer_mode_t mode);
void local_apic_busywait(int us);

int local_apic_get_tmr(uint8_t vec);
void local_apic_send_eoi();

INIT_TEXT void local_apic_send_init(int cpu);
INIT_TEXT void local_apic_send_sipi(int cpu, int vec);

void local_apic_send_ipi(int cpu, int vec);

#endif // CPU_LOCAL_APIC_H
