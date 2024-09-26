#ifndef LOAPIC_H
#define LOAPIC_H

#include <devices/acpi_madt.h>

typedef struct loapic {
    uint32_t apic_id;
    uint32_t processor_id;
    uint32_t flags;

    uint16_t cluster_id;
    uint16_t logical_id;
} loapic_t;


INIT_TEXT void loapic_alloc(size_t base, int n);
INIT_TEXT void loapic_parse(int i, madt_loapic_t *tbl);
INIT_TEXT void loapic_parse_x2(int i, madt_lox2apic_t *tbl);
INIT_TEXT int need_int_remap();
INIT_TEXT void loapic_init();

INIT_TEXT void local_apic_send_init(int cpu);
INIT_TEXT void local_apic_send_sipi(int cpu, int vec);

void loapic_timer_set_oneshot(int n);
void loapic_timer_set_periodic(int freq);
void loapic_timer_busywait(int us);

INIT_TEXT void calibrate_timer();

#endif // LOAPIC_H
